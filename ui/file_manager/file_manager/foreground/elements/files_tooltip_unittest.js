// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

var chocolateButton;
var cherriesButton;
var otherButton;
var tooltip;

var previousFocus = null;

function waitForMutation(target) {
  return new Promise(function(fulfill, reject) {
    var observer = new MutationObserver(function(mutations) {
      observer.disconnect();
      fulfill();
    });
    observer.observe(target, {attributes: true});
  });
}

function moveFocus(target) {
  if (previousFocus && previousFocus != target) {
    // Emulate blur event which would be issued in actual usage.
    // previousFocus.blur();
    // does not work here because it's not actually focused in this test.
    var blurEvent = new UIEvent('blur', {target: previousFocus});
    previousFocus.dispatchEvent(blurEvent);
  }
  previousFocus = target;
  // Emulate focus event which would be issued in actual usage.
  // target.focus();
  // does not work here because it doesn't fill sourceCapabilities in Event.
  var focusEvent =
      new UIEvent('focus', {sourceCapabilities: new InputDeviceCapabilities()});
  target.dispatchEvent(focusEvent);
}

function setUp() {
  chocolateButton = document.querySelector('#chocolate');
  cherriesButton = document.querySelector('#cherries');
  otherButton = document.querySelector('#other');
  tooltip = document.querySelector('files-tooltip');
  tooltip.addTargets([chocolateButton, cherriesButton]);
}

function testFocus(callback) {
  moveFocus(chocolateButton);

  return reportPromise(
    waitForMutation(tooltip).then(function() {
      assertEquals('Chocolate!', tooltip.textContent.trim());
      assertTrue(!!tooltip.getAttribute('visible'));
      assertEquals('4px', tooltip.style.left);
      assertEquals('70px', tooltip.style.top);

      moveFocus(cherriesButton);
      return waitForMutation(tooltip);
    }).then(function() {
      assertEquals('Cherries!', tooltip.textContent.trim());
      assertTrue(!!tooltip.getAttribute('visible'));
      var expectedLeft = document.body.offsetWidth - tooltip.offsetWidth + 'px';
      assertEquals(expectedLeft, tooltip.style.left);
      assertEquals('70px', tooltip.style.top);

      moveFocus(otherButton);
      return waitForMutation(tooltip);
    }).then(function() {
      assertFalse(!!tooltip.getAttribute('visible'));
    }), callback);
}

function testHover(callback) {
  chocolateButton.dispatchEvent(new MouseEvent('mouseover'));

  return reportPromise(
    waitForMutation(tooltip).then(function() {
      assertEquals('Chocolate!', tooltip.textContent.trim());
      assertTrue(!!tooltip.getAttribute('visible'));
      assertEquals('4px', tooltip.style.left);
      assertEquals('70px', tooltip.style.top);

      chocolateButton.dispatchEvent(new MouseEvent('mouseout'));
      cherriesButton.dispatchEvent(new MouseEvent('mouseover'));
      return waitForMutation(tooltip);
    }).then(function() {
      assertEquals('Cherries!', tooltip.textContent.trim());
      assertTrue(!!tooltip.getAttribute('visible'));
      var expectedLeft = document.body.offsetWidth - tooltip.offsetWidth + 'px';
      assertEquals(expectedLeft, tooltip.style.left);
      assertEquals('70px', tooltip.style.top);

      cherriesButton.dispatchEvent(new MouseEvent('mouseout'));
      return waitForMutation(tooltip);
    }).then(function() {
      assertFalse(!!tooltip.getAttribute('visible'));
    }), callback);
}

function testClickHides(callback) {
  chocolateButton.dispatchEvent(new MouseEvent('mouseover', {bubbles: true}));

  return reportPromise(
    waitForMutation(tooltip).then(function() {
      assertEquals('Chocolate!', tooltip.textContent.trim());
      assertTrue(!!tooltip.getAttribute('visible'));

      // Hiding here is synchronous. Dispatch the event asynchronously, so the
      // mutation observer is started before hiding.
      setTimeout(function() {
        document.body.dispatchEvent(new MouseEvent('mousedown'));
      });
      return waitForMutation(tooltip);
    }).then(function() {
      assertFalse(!!tooltip.getAttribute('visible'));
    }), callback);
}
