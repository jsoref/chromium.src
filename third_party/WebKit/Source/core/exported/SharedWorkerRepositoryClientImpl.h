/*
 * Copyright (C) 2013 Google Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef SharedWorkerRepositoryClientImpl_h
#define SharedWorkerRepositoryClientImpl_h

#include <memory>
#include "core/CoreExport.h"
#include "core/workers/SharedWorkerRepositoryClient.h"
#include "platform/wtf/Noncopyable.h"
#include "platform/wtf/PtrUtil.h"
#include "platform/wtf/RefPtr.h"

namespace blink {

class WebSharedWorkerRepositoryClient;

class CORE_EXPORT SharedWorkerRepositoryClientImpl final
    : public SharedWorkerRepositoryClient {
  WTF_MAKE_NONCOPYABLE(SharedWorkerRepositoryClientImpl);
  USING_FAST_MALLOC(SharedWorkerRepositoryClientImpl);

 public:
  static std::unique_ptr<SharedWorkerRepositoryClientImpl> Create(
      WebSharedWorkerRepositoryClient* client) {
    return WTF::WrapUnique(new SharedWorkerRepositoryClientImpl(client));
  }

  ~SharedWorkerRepositoryClientImpl() override {}

  void Connect(SharedWorker*,
               MessagePortChannel,
               const KURL&,
               const String& name, bool) override;
  void DocumentDetached(Document*) override;

 private:
  explicit SharedWorkerRepositoryClientImpl(WebSharedWorkerRepositoryClient*);

  WebSharedWorkerRepositoryClient* client_;
};

}  // namespace blink

#endif  // SharedWorkerRepositoryClientImpl_h
