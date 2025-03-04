// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

var pass = chrome.test.callbackPass;

// Constants as functions, not to be called until after runTests.
function getURLEchoUserAgent() {
  return getServerURL('echoheader?User-Agent');
}

function getURLSetCookie() {
  return getServerURL('set-cookie?Foo=Bar');
}

function getURLNonUTF8SetCookie() {
  return getServerURL('set-header?Set-Cookie%3A%20Foo%3D%FE%D1');
}

function getURLHttpSimpleLoad() {
  return getServerURL('extensions/api_test/webrequest/simpleLoad/a.html');
}
function getURLHttpXHRData() {
  return getServerURL('extensions/api_test/webrequest/xhr/data.json');
}

function toCharCodes(str) {
  var result = [];
  for (var i = 0; i < str.length; ++i) {
    result[i] = str.charCodeAt(i);
  }
  return result;
}

runTests([
  // Navigates to a page with subresources, with a blocking handler that
  // cancels the page request. The page will not load, and we should not
  // see the subresources.
  function complexLoadCancelled() {
    expect(
      [  // events
        { label: "onBeforeRequest",
          event: "onBeforeRequest",
          details: {
            type: "main_frame",
            url: getURL("complexLoad/b.html"),
            frameUrl: getURL("complexLoad/b.html"),
            initiator: getDomain(initiators.BROWSER_INITIATED)
          },
          retval: {cancel: true}
        },
        // Cancelling is considered an error.
        { label: "onErrorOccurred",
          event: "onErrorOccurred",
          details: {
            url: getURL("complexLoad/b.html"),
            fromCache: false,
            error: "net::ERR_BLOCKED_BY_CLIENT",
            initiator: getDomain(initiators.BROWSER_INITIATED)
            // Request to chrome-extension:// url has no IP.
          }
        },
      ],
      [  // event order
        ["onBeforeRequest", "onErrorOccurred"]
      ],
      {urls: ["<all_urls>"]},  // filter
      ["blocking"]);
    navigateAndWait(getURL("complexLoad/b.html"));
  },

  // Navigates to a page with subresources, with a blocking handler that
  // cancels the page request. The page will not load, and we should not
  // see the subresources.
  function simpleLoadCancelledOnReceiveHeaders() {
    expect(
      [  // events
        { label: "onBeforeRequest",
          event: "onBeforeRequest",
          details: {
            method: "GET",
            type: "main_frame",
            url: getURLHttpSimpleLoad(),
            frameUrl: getURLHttpSimpleLoad(),
            initiator: getServerDomain(initiators.BROWSER_INITIATED)
          },
          retval: {cancel: false}
        },
        { label: "onBeforeSendHeaders",
          event: "onBeforeSendHeaders",
          details: {
            url: getURLHttpSimpleLoad(),
            initiator: getServerDomain(initiators.BROWSER_INITIATED)
            // Note: no requestHeaders because we don't ask for them.
          },
        },
        { label: "onSendHeaders",
          event: "onSendHeaders",
          details: {
            url: getURLHttpSimpleLoad(),
            initiator: getServerDomain(initiators.BROWSER_INITIATED)
          }
        },
        { label: "onHeadersReceived",
          event: "onHeadersReceived",
          details: {
            url: getURLHttpSimpleLoad(),
            statusLine: "HTTP/1.1 200 OK",
            statusCode: 200,
            initiator: getServerDomain(initiators.BROWSER_INITIATED)
          },
          retval: {cancel: true}
        },
        // Cancelling is considered an error.
        { label: "onErrorOccurred",
          event: "onErrorOccurred",
          details: {
            url: getURLHttpSimpleLoad(),
            fromCache: false,
            error: "net::ERR_BLOCKED_BY_CLIENT",
            initiator: getServerDomain(initiators.BROWSER_INITIATED)
            // Request to chrome-extension:// url has no IP.
          }
        },
      ],
      [  // event order
        ["onBeforeRequest", "onBeforeSendHeaders", "onSendHeaders",
         "onHeadersReceived", "onErrorOccurred"]
      ],
      {urls: ["<all_urls>"]},  // filter
      ["blocking"]);
    navigateAndWait(getURLHttpSimpleLoad());
  },

  // Navigates to a page and provides invalid header information. The request
  // should continue as if the headers were not changed.
  function simpleLoadIgnoreOnBeforeSendHeadersInvalidHeaders() {
    expect(
      [  // events
        { label: "onBeforeRequest",
          event: "onBeforeRequest",
          details: {
            method: "GET",
            type: "main_frame",
            url: getURLHttpSimpleLoad(),
            frameUrl: getURLHttpSimpleLoad(),
            initiator: getServerDomain(initiators.BROWSER_INITIATED)
          },
        },
        { label: "onBeforeSendHeaders",
          event: "onBeforeSendHeaders",
          details: {
            url: getURLHttpSimpleLoad(),
            requestHeadersValid: true,
            initiator: getServerDomain(initiators.BROWSER_INITIATED)
          },
          retval: {requestHeaders: [{name: "User-Agent"}]}
        },
        // The headers were invalid, so they should not be modified.
        // TODO(robwu): Test whether an error is logged to the console.
        { label: "onSendHeaders",
          event: "onSendHeaders",
          details: {
            url: getURLHttpSimpleLoad(),
            requestHeadersValid: true,
            initiator: getServerDomain(initiators.BROWSER_INITIATED)
          }
        },
        { label: "onHeadersReceived",
          event: "onHeadersReceived",
          details: {
            url: getURLHttpSimpleLoad(),
            statusLine: "HTTP/1.1 200 OK",
            statusCode: 200,
            initiator: getServerDomain(initiators.BROWSER_INITIATED)
          }
        },
        { label: "onResponseStarted",
          event: "onResponseStarted",
          details: {
            url: getURLHttpSimpleLoad(),
            fromCache: false,
            statusCode: 200,
            ip: "127.0.0.1",
            statusLine: "HTTP/1.1 200 OK",
            initiator: getServerDomain(initiators.BROWSER_INITIATED)
          }
        },
        { label: "onCompleted",
          event: "onCompleted",
          details: {
            url: getURLHttpSimpleLoad(),
            fromCache: false,
            statusCode: 200,
            ip: "127.0.0.1",
            statusLine: "HTTP/1.1 200 OK",
            initiator: getServerDomain(initiators.BROWSER_INITIATED)
          }
        },
      ],
      [  // event order
        ["onBeforeRequest", "onBeforeSendHeaders", "onSendHeaders",
         "onHeadersReceived", "onResponseStarted", "onCompleted"]
      ],
      {urls: ["<all_urls>"]},  // filter
      ["blocking", "requestHeaders"]);
    navigateAndWait(getURLHttpSimpleLoad());
  },

  // Navigates to a page and provides invalid header information. The request
  // should continue as if the headers were not changed.
  function simpleLoadIgnoreOnBeforeSendHeadersInvalidResponse() {
    // Exception handling seems to break this test, so disable it.
    // See http://crbug.com/370897. TODO(robwu): Fix me.
    chrome.test.setExceptionHandler(function(){});
    expect(
      [  // events
        { label: "onBeforeRequest",
          event: "onBeforeRequest",
          details: {
            method: "GET",
            type: "main_frame",
            url: getURLHttpSimpleLoad(),
            frameUrl: getURLHttpSimpleLoad(),
            initiator: getServerDomain(initiators.BROWSER_INITIATED)
          },
        },
        { label: "onBeforeSendHeaders",
          event: "onBeforeSendHeaders",
          details: {
            url: getURLHttpSimpleLoad(),
            requestHeadersValid: true,
            initiator: getServerDomain(initiators.BROWSER_INITIATED)
          },
          retval: {foo: "bar"}
        },
        // TODO(robwu): Test whether an error is logged to the console.
        { label: "onSendHeaders",
          event: "onSendHeaders",
          details: {
            url: getURLHttpSimpleLoad(),
            requestHeadersValid: true,
            initiator: getServerDomain(initiators.BROWSER_INITIATED)
          }
        },
        { label: "onHeadersReceived",
          event: "onHeadersReceived",
          details: {
            url: getURLHttpSimpleLoad(),
            statusLine: "HTTP/1.1 200 OK",
            statusCode: 200,
            initiator: getServerDomain(initiators.BROWSER_INITIATED)
          }
        },
        { label: "onResponseStarted",
          event: "onResponseStarted",
          details: {
            url: getURLHttpSimpleLoad(),
            fromCache: false,
            statusCode: 200,
            ip: "127.0.0.1",
            statusLine: "HTTP/1.1 200 OK",
            initiator: getServerDomain(initiators.BROWSER_INITIATED)
          }
        },
        { label: "onCompleted",
          event: "onCompleted",
          details: {
            url: getURLHttpSimpleLoad(),
            fromCache: false,
            statusCode: 200,
            ip: "127.0.0.1",
            statusLine: "HTTP/1.1 200 OK",
            initiator: getServerDomain(initiators.BROWSER_INITIATED)
          }
        },
      ],
      [  // event order
        ["onBeforeRequest", "onBeforeSendHeaders", "onSendHeaders",
         "onHeadersReceived", "onResponseStarted", "onCompleted"]
      ],
      {urls: ["<all_urls>"]},  // filter
      ["blocking", "requestHeaders"]);
    navigateAndWait(getURLHttpSimpleLoad());
  },

  // Navigates to a page with a blocking handler that redirects to a different
  // page.
  function complexLoadRedirected() {
    expect(
      [  // events
        { label: "onBeforeRequest-1",
          event: "onBeforeRequest",
          details: {
            url: getURL("complexLoad/a.html"),
            frameUrl: getURL("complexLoad/a.html"),
            initiator: getDomain(initiators.BROWSER_INITIATED)
          },
          retval: {redirectUrl: getURL("simpleLoad/a.html")}
        },
        { label: "onBeforeRedirect",
          event: "onBeforeRedirect",
          details: {
            url: getURL("complexLoad/a.html"),
            redirectUrl: getURL("simpleLoad/a.html"),
            fromCache: false,
            statusLine: "HTTP/1.1 307 Internal Redirect",
            statusCode: 307,
            initiator: getDomain(initiators.BROWSER_INITIATED)
          }
        },
        { label: "onBeforeRequest-2",
          event: "onBeforeRequest",
          details: {
            url: getURL("simpleLoad/a.html"),
            frameUrl: getURL("simpleLoad/a.html"),
            initiator: getDomain(initiators.BROWSER_INITIATED)
          },
        },
        { label: "onResponseStarted",
          event: "onResponseStarted",
          details: {
            url: getURL("simpleLoad/a.html"),
            fromCache: false,
            statusCode: 200,
            statusLine: "HTTP/1.1 200 OK",
            initiator: getDomain(initiators.BROWSER_INITIATED)
            // Request to chrome-extension:// url has no IP.
          }
        },
        { label: "onCompleted",
          event: "onCompleted",
          details: {
            url: getURL("simpleLoad/a.html"),
            fromCache: false,
            statusCode: 200,
            statusLine: "HTTP/1.1 200 OK",
            initiator: getDomain(initiators.BROWSER_INITIATED)
            // Request to chrome-extension:// url has no IP.
          }
        },
      ],
      [  // event order
        ["onBeforeRequest-1", "onBeforeRedirect", "onBeforeRequest-2",
         "onResponseStarted", "onCompleted"],
      ],
      {urls: ["<all_urls>"]}, // filter
      ["blocking"]);
    navigateAndWait(getURL("complexLoad/a.html"));
  },

  // Tests redirect of <img crossorigin="anonymous" src="...">
  function crossOriginAnonymousRedirect() {
    testLoadCORSImage("anonymous");
  },

  // Tests redirect of <img crossorigin="use-credentials" src="...">
  function crossOriginCredentialedRedirect() {
    testLoadCORSImage("use-credentials");
  },

  // Loads a testserver page that echoes the User-Agent header that was
  // sent to fetch it. We modify the outgoing User-Agent in
  // onBeforeSendHeaders, so we should see that modified version.
  function modifyRequestHeaders() {
    expect(
      [  // events
        { label: "onBeforeRequest",
          event: "onBeforeRequest",
          details: {
            url: getURLEchoUserAgent(),
            frameUrl: getURLEchoUserAgent(),
            initiator: getServerDomain(initiators.BROWSER_INITIATED)
          }
        },
        { label: "onBeforeSendHeaders",
          event: "onBeforeSendHeaders",
          details: {
            url: getURLEchoUserAgent(),
            initiator: getServerDomain(initiators.BROWSER_INITIATED)
            // Note: no requestHeaders because we don't ask for them.
          },
          retval: {requestHeaders: [{name: "User-Agent", value: "FoobarUA"}]}
        },
        { label: "onSendHeaders",
          event: "onSendHeaders",
          details: {
            url: getURLEchoUserAgent(),
            initiator: getServerDomain(initiators.BROWSER_INITIATED)
          }
        },
        { label: "onHeadersReceived",
          event: "onHeadersReceived",
          details: {
            url: getURLEchoUserAgent(),
            statusLine: "HTTP/1.1 200 OK",
            statusCode: 200,
            initiator: getServerDomain(initiators.BROWSER_INITIATED)
          }
        },
        { label: "onResponseStarted",
          event: "onResponseStarted",
          details: {
            url: getURLEchoUserAgent(),
            fromCache: false,
            statusCode: 200,
            ip: "127.0.0.1",
            statusLine: "HTTP/1.1 200 OK",
            initiator: getServerDomain(initiators.BROWSER_INITIATED)
          }
        },
        { label: "onCompleted",
          event: "onCompleted",
          details: {
            url: getURLEchoUserAgent(),
            fromCache: false,
            statusCode: 200,
            ip: "127.0.0.1",
            statusLine: "HTTP/1.1 200 OK",
            initiator: getServerDomain(initiators.BROWSER_INITIATED)
          }
        },
      ],
      [  // event order
        ["onBeforeRequest", "onBeforeSendHeaders", "onSendHeaders",
         "onHeadersReceived", "onResponseStarted", "onCompleted"]
      ],
      {urls: ["<all_urls>"]}, ["blocking"]);
    // Check the page content for our modified User-Agent string.
    navigateAndWait(getURLEchoUserAgent(), function() {
      chrome.test.listenOnce(chrome.extension.onRequest, function(request) {
        chrome.test.assertTrue(request.pass, "Request header was not set.");
      });
      chrome.tabs.executeScript(tabId,
        {
          code: "chrome.extension.sendRequest(" +
              "{pass: document.body.innerText.indexOf('FoobarUA') >= 0});"
        });
    });
  },

  // Loads a testserver page that echoes the User-Agent header that was
  // sent to fetch it. We modify the outgoing User-Agent in
  // onBeforeSendHeaders, so we should see that modified version.
  // In this version we check whether we can set binary header values.
  function modifyBinaryRequestHeaders() {
    expect(
      [  // events
        { label: "onBeforeRequest",
          event: "onBeforeRequest",
          details: {
            url: getURLEchoUserAgent(),
            frameUrl: getURLEchoUserAgent(),
            initiator: getServerDomain(initiators.BROWSER_INITIATED)
          }
        },
        { label: "onBeforeSendHeaders",
          event: "onBeforeSendHeaders",
          details: {
            url: getURLEchoUserAgent(),
            initiator: getServerDomain(initiators.BROWSER_INITIATED)
            // Note: no requestHeaders because we don't ask for them.
          },
          retval: {requestHeaders: [{name: "User-Agent",
                                     binaryValue: toCharCodes("FoobarUA")}]}
        },
        { label: "onSendHeaders",
          event: "onSendHeaders",
          details: {
            url: getURLEchoUserAgent(),
            initiator: getServerDomain(initiators.BROWSER_INITIATED)
          }
        },
        { label: "onHeadersReceived",
          event: "onHeadersReceived",
          details: {
            url: getURLEchoUserAgent(),
            statusLine: "HTTP/1.1 200 OK",
            statusCode: 200,
            initiator: getServerDomain(initiators.BROWSER_INITIATED)
          }
        },
        { label: "onResponseStarted",
          event: "onResponseStarted",
          details: {
            url: getURLEchoUserAgent(),
            fromCache: false,
            statusCode: 200,
            ip: "127.0.0.1",
            statusLine: "HTTP/1.1 200 OK",
            initiator: getServerDomain(initiators.BROWSER_INITIATED)
          }
        },
        { label: "onCompleted",
          event: "onCompleted",
          details: {
            url: getURLEchoUserAgent(),
            fromCache: false,
            statusCode: 200,
            ip: "127.0.0.1",
            statusLine: "HTTP/1.1 200 OK",
            initiator: getServerDomain(initiators.BROWSER_INITIATED)
          }
        },
      ],
      [  // event order
        ["onBeforeRequest", "onBeforeSendHeaders", "onSendHeaders",
         "onHeadersReceived", "onResponseStarted", "onCompleted"]
      ],
      {urls: ["<all_urls>"]}, ["blocking"]);
    // Check the page content for our modified User-Agent string.
    navigateAndWait(getURLEchoUserAgent(), function() {
      chrome.test.listenOnce(chrome.extension.onRequest, function(request) {
        chrome.test.assertTrue(request.pass, "Request header was not set.");
      });
      chrome.tabs.executeScript(tabId,
        {
          code: "chrome.extension.sendRequest(" +
              "{pass: document.body.innerText.indexOf('FoobarUA') >= 0});"
        });
    });
  },

  // Loads a testserver page that sets a cookie "Foo=Bar" but removes
  // the cookies from the response headers so that they are not set.
  function modifyResponseHeaders() {
    expect(
      [  // events
        { label: "onBeforeRequest",
          event: "onBeforeRequest",
          details: {
            method: "GET",
            type: "main_frame",
            url: getURLSetCookie(),
            frameUrl: getURLSetCookie(),
            initiator: getServerDomain(initiators.BROWSER_INITIATED)
          }
        },
        { label: "onBeforeSendHeaders",
          event: "onBeforeSendHeaders",
          details: {
            url: getURLSetCookie(),
            initiator: getServerDomain(initiators.BROWSER_INITIATED)
            // Note: no requestHeaders because we don't ask for them.
          },
        },
        { label: "onSendHeaders",
          event: "onSendHeaders",
          details: {
            url: getURLSetCookie(),
            initiator: getServerDomain(initiators.BROWSER_INITIATED)
          }
        },
        { label: "onHeadersReceived",
          event: "onHeadersReceived",
          details: {
            url: getURLSetCookie(),
            statusLine: "HTTP/1.1 200 OK",
            statusCode: 200,
            responseHeadersExist: true,
            initiator: getServerDomain(initiators.BROWSER_INITIATED)
          },
          retval_function: function(name, details) {
            responseHeaders = details.responseHeaders;
            var found = false;
            for (var i = 0; i < responseHeaders.length; ++i) {
              if (responseHeaders[i].name === "Set-Cookie" &&
                  responseHeaders[i].value.indexOf("Foo") != -1) {
                found = true;
                responseHeaders.splice(i);
                break;
              }
            }
            chrome.test.assertTrue(found);
            return {responseHeaders: responseHeaders};
          }
        },
        { label: "onResponseStarted",
          event: "onResponseStarted",
          details: {
            url: getURLSetCookie(),
            fromCache: false,
            statusCode: 200,
            statusLine: "HTTP/1.1 200 OK",
            ip: "127.0.0.1",
            responseHeadersExist: true,
            initiator: getServerDomain(initiators.BROWSER_INITIATED)
          }
        },
        { label: "onCompleted",
          event: "onCompleted",
          details: {
            url: getURLSetCookie(),
            fromCache: false,
            statusCode: 200,
            statusLine: "HTTP/1.1 200 OK",
            ip: "127.0.0.1",
            responseHeadersExist: true,
            initiator: getServerDomain(initiators.BROWSER_INITIATED)
          }
        },
      ],
      [  // event order
        ["onBeforeRequest", "onBeforeSendHeaders", "onSendHeaders",
         "onHeadersReceived", "onResponseStarted", "onCompleted"]
      ],
      {urls: ["<all_urls>"]}, ["blocking", "responseHeaders"]);
    // Check that the cookie was really removed.
    navigateAndWait(getURLSetCookie(), function() {
      chrome.test.listenOnce(chrome.extension.onRequest, function(request) {
        chrome.test.assertTrue(request.pass, "Cookie was not removed.");
      });
      chrome.tabs.executeScript(tabId,
      { code: "chrome.extension.sendRequest(" +
            "{pass: document.cookie.indexOf('Foo') == -1});"
        });
    });
  },

  // Loads a testserver page that sets a cookie "Foo=U+FDD1" which is not a
  // valid UTF-8 code point. Therefore, it cannot be passed to JavaScript
  // as a normal string.
  function handleNonUTF8InModifyResponseHeaders() {
    expect(
      [  // events
        { label: "onBeforeRequest",
          event: "onBeforeRequest",
          details: {
            method: "GET",
            type: "main_frame",
            url: getURLNonUTF8SetCookie(),
            frameUrl: getURLNonUTF8SetCookie(),
            initiator: getServerDomain(initiators.BROWSER_INITIATED)
          }
        },
        { label: "onBeforeSendHeaders",
          event: "onBeforeSendHeaders",
          details: {
            url: getURLNonUTF8SetCookie(),
            initiator: getServerDomain(initiators.BROWSER_INITIATED)
            // Note: no requestHeaders because we don't ask for them.
          },
        },
        { label: "onSendHeaders",
          event: "onSendHeaders",
          details: {
            url: getURLNonUTF8SetCookie(),
            initiator: getServerDomain(initiators.BROWSER_INITIATED)
          }
        },
        { label: "onHeadersReceived",
          event: "onHeadersReceived",
          details: {
            url: getURLNonUTF8SetCookie(),
            statusLine: "HTTP/1.1 200 OK",
            statusCode: 200,
            responseHeadersExist: true,
            initiator: getServerDomain(initiators.BROWSER_INITIATED)
          },
          retval_function: function(name, details) {
            responseHeaders = details.responseHeaders;
            var found = false;
            var expectedValue = [
              "F".charCodeAt(0),
              "o".charCodeAt(0),
              "o".charCodeAt(0),
              0x3D, 0xFE, 0xD1
              ];

            for (var i = 0; i < responseHeaders.length; ++i) {
              if (responseHeaders[i].name === "Set-Cookie" &&
                  deepEq(responseHeaders[i].binaryValue, expectedValue)) {
                found = true;
                responseHeaders.splice(i);
                break;
              }
            }
            chrome.test.assertTrue(found);
            return {responseHeaders: responseHeaders};
          }
        },
        { label: "onResponseStarted",
          event: "onResponseStarted",
          details: {
            url: getURLNonUTF8SetCookie(),
            fromCache: false,
            statusCode: 200,
            statusLine: "HTTP/1.1 200 OK",
            ip: "127.0.0.1",
            responseHeadersExist: true,
            initiator: getServerDomain(initiators.BROWSER_INITIATED)
          }
        },
        { label: "onCompleted",
          event: "onCompleted",
          details: {
            url: getURLNonUTF8SetCookie(),
            fromCache: false,
            statusCode: 200,
            statusLine: "HTTP/1.1 200 OK",
            ip: "127.0.0.1",
            responseHeadersExist: true,
            initiator: getServerDomain(initiators.BROWSER_INITIATED)
          }
        },
      ],
      [  // event order
        ["onBeforeRequest", "onBeforeSendHeaders", "onSendHeaders",
         "onHeadersReceived", "onResponseStarted", "onCompleted"]
      ],
      {urls: ["<all_urls>"]}, ["blocking", "responseHeaders"]);
    // Check that the cookie was really removed.
    navigateAndWait(getURLNonUTF8SetCookie(), function() {
      chrome.test.listenOnce(chrome.extension.onRequest, function(request) {
        chrome.test.assertTrue(request.pass, "Cookie was not removed.");
      });
      chrome.tabs.executeScript(tabId,
      { code: "chrome.extension.sendRequest(" +
            "{pass: document.cookie.indexOf('Foo') == -1});"
        });
    });
  },

  // Navigates to a page with a blocking handler that redirects to a different
  // non-http page during onHeadersReceived. The requested page should not be
  // loaded, and the redirect should succeed.
  function simpleLoadRedirectOnReceiveHeaders() {
    expect(
      [  // events
        { label: "onBeforeRequest-1",
          event: "onBeforeRequest",
          details: {
            method: "GET",
            type: "main_frame",
            url: getURLHttpSimpleLoad(),
            frameUrl: getURLHttpSimpleLoad(),
            initiator: getServerDomain(initiators.BROWSER_INITIATED)
          },
        },
        { label: "onBeforeSendHeaders",
          event: "onBeforeSendHeaders",
          details: {
            url: getURLHttpSimpleLoad(),
            initiator: getServerDomain(initiators.BROWSER_INITIATED)
            // Note: no requestHeaders because we don't ask for them.
          },
        },
        { label: "onSendHeaders",
          event: "onSendHeaders",
          details: {
            url: getURLHttpSimpleLoad(),
            initiator: getServerDomain(initiators.BROWSER_INITIATED)
          }
        },
        { label: "onHeadersReceived",
          event: "onHeadersReceived",
          details: {
            url: getURLHttpSimpleLoad(),
            statusLine: "HTTP/1.1 200 OK",
            statusCode: 200,
            initiator: getServerDomain(initiators.BROWSER_INITIATED)
          },
          retval: {redirectUrl: getURL("simpleLoad/a.html")}
        },
        { label: "onBeforeRedirect",
          event: "onBeforeRedirect",
          details: {
            url: getURLHttpSimpleLoad(),
            redirectUrl: getURL("simpleLoad/a.html"),
            statusLine: "HTTP/1.1 302 Found",
            statusCode: 302,
            fromCache: false,
            ip: "127.0.0.1",
            initiator: getServerDomain(initiators.BROWSER_INITIATED)
          }
        },
        { label: "onBeforeRequest-2",
          event: "onBeforeRequest",
          details: {
            url: getURL("simpleLoad/a.html"),
            frameUrl: getURL("simpleLoad/a.html"),
            initiator: getServerDomain(initiators.BROWSER_INITIATED)
          },
        },
        { label: "onResponseStarted",
          event: "onResponseStarted",
          details: {
            url: getURL("simpleLoad/a.html"),
            fromCache: false,
            statusCode: 200,
            statusLine: "HTTP/1.1 200 OK",
            initiator: getServerDomain(initiators.BROWSER_INITIATED)
            // Request to chrome-extension:// url has no IP.
          }
        },
        { label: "onCompleted",
          event: "onCompleted",
          details: {
            url: getURL("simpleLoad/a.html"),
            fromCache: false,
            statusCode: 200,
            statusLine: "HTTP/1.1 200 OK",
            initiator: getServerDomain(initiators.BROWSER_INITIATED)
            // Request to chrome-extension:// url has no IP.
          }
        },
      ],
      [  // event order
        ["onBeforeRequest-1", "onBeforeSendHeaders", "onSendHeaders",
         "onHeadersReceived", "onBeforeRedirect", "onBeforeRequest-2",
         "onResponseStarted", "onCompleted"]
      ],
      {urls: ["<all_urls>"]},  // filter
      ["blocking"]);
    navigateAndWait(getURLHttpSimpleLoad());
  },

  // Checks that synchronous XHR requests from ourself are invisible to
  // blocking handlers.
  function syncXhrsFromOurselfAreInvisible() {
    expect(
      [  // events
        { label: "a-onBeforeRequest",
          event: "onBeforeRequest",
          details: {
            url: getURL("simpleLoad/a.html"),
            frameUrl: getURL("simpleLoad/a.html"),
            initiator: getDomain(initiators.BROWSER_INITIATED)
          }
        },
        { label: "a-onResponseStarted",
          event: "onResponseStarted",
          details: {
            url: getURL("simpleLoad/a.html"),
            statusCode: 200,
            fromCache: false,
            statusLine: "HTTP/1.1 200 OK",
            initiator: getDomain(initiators.BROWSER_INITIATED)
            // Request to chrome-extension:// url has no IP.
          }
        },
        { label: "a-onCompleted",
          event: "onCompleted",
          details: {
            url: getURL("simpleLoad/a.html"),
            statusCode: 200,
            fromCache: false,
            statusLine: "HTTP/1.1 200 OK",
            initiator: getDomain(initiators.BROWSER_INITIATED)
            // Request to chrome-extension:// url has no IP.
          }
        },
        // We do not see onBeforeRequest for the XHR request here because it
        // is handled by a blocking handler.
        { label: "x-onSendHeaders",
          event: "onSendHeaders",
          details: {
            url: getURLHttpXHRData(),
            tabId: 1,
            type: "xmlhttprequest",
            initiator: getDomain(initiators.WEB_INITIATED)
          }
        },
        { label: "x-onResponseStarted",
          event: "onResponseStarted",
          details: {
            url: getURLHttpXHRData(),
            statusCode: 200,
            fromCache: false,
            statusLine: "HTTP/1.1 200 OK",
            tabId: 1,
            type: "xmlhttprequest",
            ip: "127.0.0.1",
            initiator: getDomain(initiators.WEB_INITIATED)
            // Request to chrome-extension:// url has no IP.
          }
        },
        { label: "x-onCompleted",
          event: "onCompleted",
          details: {
            url: getURLHttpXHRData(),
            statusCode: 200,
            fromCache: false,
            statusLine: "HTTP/1.1 200 OK",
            tabId: 1,
            type: "xmlhttprequest",
            ip: "127.0.0.1",
            initiator: getDomain(initiators.WEB_INITIATED)
            // Request to chrome-extension:// url has no IP.
          }
        },
        { label: "b-onBeforeRequest",
          event: "onBeforeRequest",
          details: {
            url: getURL("complexLoad/b.jpg"),
            frameUrl: getURL("complexLoad/b.jpg"),
            initiator: getDomain(initiators.BROWSER_INITIATED)
          }
        },
        { label: "b-onResponseStarted",
          event: "onResponseStarted",
          details: {
            url: getURL("complexLoad/b.jpg"),
            statusCode: 200,
            fromCache: false,
            statusLine: "HTTP/1.1 200 OK",
            initiator: getDomain(initiators.BROWSER_INITIATED)
            // Request to chrome-extension:// url has no IP.
          }
        },
        { label: "b-onCompleted",
          event: "onCompleted",
          details: {
            url: getURL("complexLoad/b.jpg"),
            statusCode: 200,
            fromCache: false,
            statusLine: "HTTP/1.1 200 OK",
            initiator: getDomain(initiators.BROWSER_INITIATED)
            // Request to chrome-extension:// url has no IP.
          }
        },
      ],
      [  // event order
        ["a-onBeforeRequest", "a-onResponseStarted", "a-onCompleted",
         "x-onSendHeaders", "x-onResponseStarted", "x-onCompleted",
         "b-onBeforeRequest", "b-onResponseStarted", "b-onCompleted"]
      ],
      {urls: ["<all_urls>"]}, ["blocking"]);
    // Check the page content for our modified User-Agent string.
    navigateAndWait(getURL("simpleLoad/a.html"), function() {
        var req = new XMLHttpRequest();
        var asynchronous = false;
        req.open("GET", getURLHttpXHRData(), asynchronous);
        req.send(null);
        navigateAndWait(getURL("complexLoad/b.jpg"));
    });
  },

  // Checks that asynchronous XHR requests from ourself are visible to
  // blocking handlers.
  function asyncXhrsFromOurselfAreVisible() {
    expect(
      [  // events
        { label: "a-onBeforeRequest",
          event: "onBeforeRequest",
          details: {
            url: getURL("simpleLoad/a.html"),
            frameUrl: getURL("simpleLoad/a.html"),
            initiator: getDomain(initiators.BROWSER_INITIATED)
          }
        },
        { label: "a-onResponseStarted",
          event: "onResponseStarted",
          details: {
            url: getURL("simpleLoad/a.html"),
            statusCode: 200,
            fromCache: false,
            statusLine: "HTTP/1.1 200 OK",
            initiator: getDomain(initiators.BROWSER_INITIATED)
            // Request to chrome-extension:// url has no IP.
          }
        },
        { label: "a-onCompleted",
          event: "onCompleted",
          details: {
            url: getURL("simpleLoad/a.html"),
            statusCode: 200,
            fromCache: false,
            statusLine: "HTTP/1.1 200 OK",
            initiator: getDomain(initiators.BROWSER_INITIATED)
            // Request to chrome-extension:// url has no IP.
          }
        },
        {
          label: "x-onBeforeRequest",
          event: "onBeforeRequest",
          details: {
            url: getURLHttpXHRData(),
            tabId: 1,
            type: "xmlhttprequest",
            frameUrl: "unknown frame URL",
            initiator: getDomain(initiators.WEB_INITIATED)
          }
        },
        {
          label: "x-onBeforeSendHeaders",
          event: "onBeforeSendHeaders",
          details: {
            url: getURLHttpXHRData(),
            tabId: 1,
            type: "xmlhttprequest",
            initiator: getDomain(initiators.WEB_INITIATED)
          }
        },
        { label: "x-onSendHeaders",
          event: "onSendHeaders",
          details: {
            url: getURLHttpXHRData(),
            tabId: 1,
            type: "xmlhttprequest",
            initiator: getDomain(initiators.WEB_INITIATED)
          }
        },
        { label: "x-onResponseStarted",
          event: "onResponseStarted",
          details: {
            url: getURLHttpXHRData(),
            statusCode: 200,
            fromCache: false,
            statusLine: "HTTP/1.1 200 OK",
            tabId: 1,
            type: "xmlhttprequest",
            ip: "127.0.0.1",
            // Request to chrome-extension:// url has no IP.
            initiator: getDomain(initiators.WEB_INITIATED)
          }
        },
        {
          label: "x-onHeadersReceived",
          event: "onHeadersReceived",
          details: {
            url: getURLHttpXHRData(),
            tabId: 1,
            type: "xmlhttprequest",
            statusLine: "HTTP/1.1 200 OK",
            statusCode: 200,
            initiator: getDomain(initiators.WEB_INITIATED)
          }
        },
        { label: "x-onCompleted",
          event: "onCompleted",
          details: {
            url: getURLHttpXHRData(),
            statusCode: 200,
            fromCache: false,
            statusLine: "HTTP/1.1 200 OK",
            tabId: 1,
            type: "xmlhttprequest",
            ip: "127.0.0.1",
            // Request to chrome-extension:// url has no IP.
            initiator: getDomain(initiators.WEB_INITIATED)
          }
        },
        { label: "b-onBeforeRequest",
          event: "onBeforeRequest",
          details: {
            url: getURL("complexLoad/b.jpg"),
            frameUrl: getURL("complexLoad/b.jpg"),
            initiator: getDomain(initiators.BROWSER_INITIATED)
          }
        },
        { label: "b-onResponseStarted",
          event: "onResponseStarted",
          details: {
            url: getURL("complexLoad/b.jpg"),
            statusCode: 200,
            fromCache: false,
            statusLine: "HTTP/1.1 200 OK",
            initiator: getDomain(initiators.BROWSER_INITIATED)
            // Request to chrome-extension:// url has no IP.
          }
        },
        { label: "b-onCompleted",
          event: "onCompleted",
          details: {
            url: getURL("complexLoad/b.jpg"),
            statusCode: 200,
            fromCache: false,
            statusLine: "HTTP/1.1 200 OK",
            initiator: getDomain(initiators.BROWSER_INITIATED)
            // Request to chrome-extension:// url has no IP.
          }
        },
      ],
      [  // event order
        ["a-onBeforeRequest", "a-onResponseStarted", "a-onCompleted",
         "x-onBeforeRequest", "x-onBeforeSendHeaders", "x-onSendHeaders",
         "x-onHeadersReceived", "x-onResponseStarted", "x-onCompleted"],
        ["a-onCompleted", "x-onBeforeRequest",
         "b-onBeforeRequest", "b-onResponseStarted", "b-onCompleted"]
      ],
      {urls: ["<all_urls>"]}, ["blocking"]);
    // Check the page content for our modified User-Agent string.
    navigateAndWait(getURL("simpleLoad/a.html"), function() {
        var req = new XMLHttpRequest();
        var asynchronous = true;
        req.open("GET", getURLHttpXHRData(), asynchronous);
        req.send(null);
        navigateAndWait(getURL("complexLoad/b.jpg"));
    });
  },
]);


// This helper verifies that extensions can successfully redirect resources even
// if cross-origin access control is in effect via the crossorigin attribute.
// Used by crossOriginAnonymousRedirect and crossOriginCredentialedRedirect.
function testLoadCORSImage(crossOriginAttributeValue) {
  // (Non-existent) image URL, with random query string to bust the cache.
  var requestedUrl = getServerURL("cors/intercepted_by_extension.gif?" +
                                  Math.random(), "original.tld");
  var frameUrl = getServerURL(
      "extensions/api_test/webrequest/cors/load_image.html?" +
      "crossOrigin=" + crossOriginAttributeValue +
      "&src=" + encodeURIComponent(requestedUrl));
  var redirectTarget = getServerURL(
      "extensions/api_test/webrequest/cors/redirect_target.gif", "domain.tld");
  var initiator = getServerDomain(initiators.WEB_INITIATED);
  expect(
    [  // events
      { label: "onBeforeRequest-1",
        event: "onBeforeRequest",
        details: {
          type: "image",
          url: requestedUrl,
          // Frame URL unavailable because requests are filtered by type=image.
          frameUrl: "unknown frame URL",
          initiator: initiator
        },
        retval: {redirectUrl: redirectTarget}
      },
      { label: "onBeforeRedirect",
        event: "onBeforeRedirect",
        details: {
          type: "image",
          url: requestedUrl,
          redirectUrl: redirectTarget,
          statusLine: "HTTP/1.1 307 Internal Redirect",
          statusCode: 307,
          fromCache: false,
          initiator: initiator
        }
      },
      { label: "onBeforeRequest-2",
        event: "onBeforeRequest",
        details: {
          type: "image",
          url: redirectTarget,
          // Frame URL unavailable because requests are filtered by type=image.
          frameUrl: "unknown frame URL",
          initiator: initiator
        },
      },
      {
        label: "onBeforeSendHeaders",
        event: "onBeforeSendHeaders",
        details: {
          type: "image",
          url: redirectTarget,
          initiator: initiator
        }
      },
      {
        label: "onSendHeaders",
        event: "onSendHeaders",
        details: {
          type: "image",
          url: redirectTarget,
          initiator: initiator
        }
      },
      {
        label: "onHeadersReceived",
        event: "onHeadersReceived",
        details: {
          type: "image",
          url: redirectTarget,
          statusLine: "HTTP/1.1 200 OK",
          statusCode: 200,
          initiator: initiator
        }
      },
      { label: "onResponseStarted",
        event: "onResponseStarted",
        details: {
          type: "image",
          url: redirectTarget,
          fromCache: false,
          statusCode: 200,
          ip: "127.0.0.1",
          statusLine: "HTTP/1.1 200 OK",
          initiator: initiator
        }
      },
      { label: "onCompleted",
        event: "onCompleted",
        details: {
          type: "image",
          url: redirectTarget,
          fromCache: false,
          statusCode: 200,
          ip: "127.0.0.1",
          statusLine: "HTTP/1.1 200 OK",
          initiator: initiator
        }
      },
      // After the image loads, the test will load the following URL
      // to signal that the test succeeded.
      {
        label: "onBeforeRequest-3",
        event: "onBeforeRequest",
        details: {
          type: "image",
          url: getServerURL("signal_that_image_loaded_successfully"),
          // Frame URL unavailable because requests are filtered by type=image.
          frameUrl: "unknown frame URL",
          initiator: initiator
        },
        retval: {cancel: true}
      },
      { label: "onErrorOccurred",
        event: "onErrorOccurred",
        details: {
          type: "image",
          url: getServerURL("signal_that_image_loaded_successfully"),
          fromCache: false,
          error: "net::ERR_BLOCKED_BY_CLIENT",
          initiator: initiator
        }
      },
    ],
    [  // event order
      ["onBeforeRequest-1", "onBeforeRedirect", "onBeforeRequest-2",
       "onBeforeSendHeaders", "onSendHeaders", "onHeadersReceived",
       "onResponseStarted", "onCompleted",
       "onBeforeRequest-3", "onErrorOccurred"],
    ],
    {urls: ["<all_urls>"], types: ['image']}, // filter
    ["blocking"]);
  navigateAndWait(frameUrl);
}
