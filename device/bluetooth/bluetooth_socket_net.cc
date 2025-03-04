// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "device/bluetooth/bluetooth_socket_net.h"

#include <memory>
#include <string>
#include <utility>

#include "base/containers/queue.h"
#include "base/location.h"
#include "base/logging.h"
#include "base/memory/linked_ptr.h"
#include "base/memory/ref_counted.h"
#include "base/sequenced_task_runner.h"
#include "base/threading/thread_restrictions.h"
#include "device/bluetooth/bluetooth_socket.h"
#include "device/bluetooth/bluetooth_socket_thread.h"
#include "net/base/io_buffer.h"
#include "net/base/net_errors.h"
#include "net/log/net_log_source.h"

namespace {

const char kSocketNotConnected[] = "Socket is not connected.";

static void DeactivateSocket(
    const scoped_refptr<device::BluetoothSocketThread>& socket_thread) {
  socket_thread->OnSocketDeactivate();
}

}  // namespace

namespace device {

BluetoothSocketNet::WriteRequest::WriteRequest()
    : buffer_size(0) {}

BluetoothSocketNet::WriteRequest::~WriteRequest() {}

BluetoothSocketNet::BluetoothSocketNet(
    scoped_refptr<base::SequencedTaskRunner> ui_task_runner,
    scoped_refptr<BluetoothSocketThread> socket_thread)
    : ui_task_runner_(ui_task_runner),
      socket_thread_(socket_thread) {
  DCHECK(ui_task_runner->RunsTasksInCurrentSequence());
  socket_thread_->OnSocketActivate();
}

BluetoothSocketNet::~BluetoothSocketNet() {
  DCHECK(!tcp_socket_);
  ui_task_runner_->PostTask(FROM_HERE,
                            base::Bind(&DeactivateSocket, socket_thread_));
}

void BluetoothSocketNet::Close() {
  DCHECK(ui_task_runner_->RunsTasksInCurrentSequence());
  socket_thread_->task_runner()->PostTask(
      FROM_HERE, base::Bind(&BluetoothSocketNet::DoClose, this));
}

void BluetoothSocketNet::Disconnect(
    const base::Closure& success_callback) {
  DCHECK(ui_task_runner_->RunsTasksInCurrentSequence());
  socket_thread_->task_runner()->PostTask(
      FROM_HERE,
      base::Bind(
          &BluetoothSocketNet::DoDisconnect,
          this,
          base::Bind(&BluetoothSocketNet::PostSuccess,
                     this,
                     success_callback)));
}

void BluetoothSocketNet::Receive(
    int buffer_size,
    const ReceiveCompletionCallback& success_callback,
    const ReceiveErrorCompletionCallback& error_callback) {
  DCHECK(ui_task_runner_->RunsTasksInCurrentSequence());
  socket_thread_->task_runner()->PostTask(
      FROM_HERE,
      base::Bind(
          &BluetoothSocketNet::DoReceive,
          this,
          buffer_size,
          base::Bind(&BluetoothSocketNet::PostReceiveCompletion,
                     this,
                     success_callback),
          base::Bind(&BluetoothSocketNet::PostReceiveErrorCompletion,
                     this,
                     error_callback)));
}

void BluetoothSocketNet::Send(
    scoped_refptr<net::IOBuffer> buffer,
    int buffer_size,
    const SendCompletionCallback& success_callback,
    const ErrorCompletionCallback& error_callback) {
  DCHECK(ui_task_runner_->RunsTasksInCurrentSequence());
  socket_thread_->task_runner()->PostTask(
      FROM_HERE,
      base::Bind(
          &BluetoothSocketNet::DoSend,
          this,
          buffer,
          buffer_size,
          base::Bind(&BluetoothSocketNet::PostSendCompletion,
                     this,
                     success_callback),
          base::Bind(&BluetoothSocketNet::PostErrorCompletion,
                     this,
                     error_callback)));
}

void BluetoothSocketNet::ResetData() {
}

void BluetoothSocketNet::ResetTCPSocket() {
  tcp_socket_.reset(new net::TCPSocket(NULL, NULL, net::NetLogSource()));
}

void BluetoothSocketNet::SetTCPSocket(
    std::unique_ptr<net::TCPSocket> tcp_socket) {
  tcp_socket_ = std::move(tcp_socket);
}

void BluetoothSocketNet::PostSuccess(const base::Closure& callback) {
  ui_task_runner_->PostTask(FROM_HERE, callback);
}

void BluetoothSocketNet::PostErrorCompletion(
    const ErrorCompletionCallback& callback,
    const std::string& error) {
  ui_task_runner_->PostTask(FROM_HERE, base::Bind(callback, error));
}

void BluetoothSocketNet::DoClose() {
  DCHECK(socket_thread_->task_runner()->RunsTasksInCurrentSequence());
  base::ThreadRestrictions::AssertIOAllowed();

  if (tcp_socket_) {
    tcp_socket_->Close();
    tcp_socket_.reset(NULL);
  }

  // Note: Closing |tcp_socket_| above released all potential pending
  // Send/Receive operations, so we can no safely release the state associated
  // to those pending operations.
  read_buffer_ = NULL;
  base::queue<linked_ptr<WriteRequest>> empty;
  std::swap(write_queue_, empty);

  ResetData();
}

void BluetoothSocketNet::DoDisconnect(const base::Closure& callback) {
  DCHECK(socket_thread_->task_runner()->RunsTasksInCurrentSequence());
  base::ThreadRestrictions::AssertIOAllowed();

  DoClose();
  callback.Run();
}

void BluetoothSocketNet::DoReceive(
    int buffer_size,
    const ReceiveCompletionCallback& success_callback,
    const ReceiveErrorCompletionCallback& error_callback) {
  DCHECK(socket_thread_->task_runner()->RunsTasksInCurrentSequence());
  base::ThreadRestrictions::AssertIOAllowed();

  if (!tcp_socket_) {
    error_callback.Run(BluetoothSocket::kDisconnected, kSocketNotConnected);
    return;
  }

  // Only one pending read at a time
  if (read_buffer_.get()) {
    error_callback.Run(BluetoothSocket::kIOPending,
                       net::ErrorToString(net::ERR_IO_PENDING));
    return;
  }

  scoped_refptr<net::IOBufferWithSize> buffer(
      new net::IOBufferWithSize(buffer_size));
  int read_result =
      tcp_socket_->Read(buffer.get(),
                        buffer->size(),
                        base::Bind(&BluetoothSocketNet::OnSocketReadComplete,
                                   this,
                                   success_callback,
                                   error_callback));

  read_buffer_ = buffer;
  if (read_result != net::ERR_IO_PENDING)
    OnSocketReadComplete(success_callback, error_callback, read_result);
}

void BluetoothSocketNet::OnSocketReadComplete(
    const ReceiveCompletionCallback& success_callback,
    const ReceiveErrorCompletionCallback& error_callback,
    int read_result) {
  DCHECK(socket_thread_->task_runner()->RunsTasksInCurrentSequence());
  base::ThreadRestrictions::AssertIOAllowed();

  scoped_refptr<net::IOBufferWithSize> buffer;
  buffer.swap(read_buffer_);
  if (read_result > 0) {
    success_callback.Run(read_result, buffer);
  } else if (read_result == net::OK ||
             read_result == net::ERR_CONNECTION_CLOSED ||
             read_result == net::ERR_CONNECTION_RESET) {
    error_callback.Run(BluetoothSocket::kDisconnected,
                       net::ErrorToString(read_result));
  } else {
    error_callback.Run(BluetoothSocket::kSystemError,
                       net::ErrorToString(read_result));
  }
}

void BluetoothSocketNet::DoSend(
    scoped_refptr<net::IOBuffer> buffer,
    int buffer_size,
    const SendCompletionCallback& success_callback,
    const ErrorCompletionCallback& error_callback) {
  DCHECK(socket_thread_->task_runner()->RunsTasksInCurrentSequence());
  base::ThreadRestrictions::AssertIOAllowed();

  if (!tcp_socket_) {
    error_callback.Run(kSocketNotConnected);
    return;
  }

  linked_ptr<WriteRequest> request(new WriteRequest());
  request->buffer = buffer;
  request->buffer_size = buffer_size;
  request->success_callback = success_callback;
  request->error_callback = error_callback;

  write_queue_.push(request);
  if (write_queue_.size() == 1) {
    SendFrontWriteRequest();
  }
}

void BluetoothSocketNet::SendFrontWriteRequest() {
  DCHECK(socket_thread_->task_runner()->RunsTasksInCurrentSequence());
  base::ThreadRestrictions::AssertIOAllowed();

  if (!tcp_socket_)
    return;

  if (write_queue_.size() == 0)
    return;

  linked_ptr<WriteRequest> request = write_queue_.front();
  net::CompletionCallback callback =
      base::Bind(&BluetoothSocketNet::OnSocketWriteComplete,
                 this,
                 request->success_callback,
                 request->error_callback);
  int send_result =
      tcp_socket_->Write(request->buffer.get(), request->buffer_size, callback);
  if (send_result != net::ERR_IO_PENDING) {
    callback.Run(send_result);
  }
}

void BluetoothSocketNet::OnSocketWriteComplete(
    const SendCompletionCallback& success_callback,
    const ErrorCompletionCallback& error_callback,
    int send_result) {
  DCHECK(socket_thread_->task_runner()->RunsTasksInCurrentSequence());
  base::ThreadRestrictions::AssertIOAllowed();

  write_queue_.pop();

  if (send_result >= net::OK) {
    success_callback.Run(send_result);
  } else {
    error_callback.Run(net::ErrorToString(send_result));
  }

  // Don't call directly to avoid potentail large recursion.
  socket_thread_->task_runner()->PostNonNestableTask(
      FROM_HERE,
      base::Bind(&BluetoothSocketNet::SendFrontWriteRequest, this));
}

void BluetoothSocketNet::PostReceiveCompletion(
    const ReceiveCompletionCallback& callback,
    int io_buffer_size,
    scoped_refptr<net::IOBuffer> io_buffer) {
  ui_task_runner_->PostTask(FROM_HERE,
                            base::Bind(callback, io_buffer_size, io_buffer));
}

void BluetoothSocketNet::PostReceiveErrorCompletion(
    const ReceiveErrorCompletionCallback& callback,
    ErrorReason reason,
    const std::string& error_message) {
  ui_task_runner_->PostTask(FROM_HERE,
                            base::Bind(callback, reason, error_message));
}

void BluetoothSocketNet::PostSendCompletion(
    const SendCompletionCallback& callback,
    int bytes_written) {
  ui_task_runner_->PostTask(FROM_HERE, base::Bind(callback, bytes_written));
}

}  // namespace device
