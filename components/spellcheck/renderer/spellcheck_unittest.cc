// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


// Make sure all misspellings can be found in a relatively long sentence.
TEST_F(SpellCheckTest, SpellCheckParagraphLongSentenceMultipleMisspellings) {
  std::vector<SpellCheckResult> expected;

  // All 'the' are converted to 'hte' in US consitition preamble.
  const base::string16 text = base::UTF8ToUTF16(
