/*
 * Copyright (C) 1999 Lars Knoll (knoll@kde.org)
 *           (C) 1999 Antti Koivisto (koivisto@kde.org)
 * Copyright (C) 2003, 2004, 2005, 2006, 2007, 2008 Apple Inc. All rights
 * reserved.
 * Copyright (C) 2014 Samsung Electronics. All rights reserved.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public License
 * along with this library; see the file COPYING.LIB.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 *
 */

#ifndef HTMLFormControlsCollection_h
#define HTMLFormControlsCollection_h

#include "core/html/HTMLCollection.h"
#include "core/html/HTMLElement.h"
#include "core/html/forms/ListedElement.h"
#include "core/html/forms/RadioNodeList.h"

namespace blink {

class HTMLImageElement;
class RadioNodeListOrElement;

// This class is just a big hack to find form elements even in malformed HTML
// elements.  The famous <table><tr><form><td> problem.

class HTMLFormControlsCollection final : public HTMLCollection {
  DEFINE_WRAPPERTYPEINFO();

 public:
  static HTMLFormControlsCollection* Create(ContainerNode&, CollectionType);

  ~HTMLFormControlsCollection() override;

  HTMLElement* item(unsigned offset) const {
    return ToHTMLElement(HTMLCollection::item(offset));
  }

  HTMLElement* namedItem(const AtomicString& name) const override;
  void namedGetter(const AtomicString& name, RadioNodeListOrElement&);

  DECLARE_VIRTUAL_TRACE();

 private:
  explicit HTMLFormControlsCollection(ContainerNode&);

  void UpdateIdNameCache() const override;
  void SupportedPropertyNames(Vector<String>& names) override;

  const ListedElement::List& ListedElements() const;
  const HeapVector<Member<HTMLImageElement>>& FormImageElements() const;
  HTMLElement* VirtualItemAfter(Element*) const override;
  void InvalidateCache(Document* old_document = 0) const override;

  mutable Member<HTMLElement> cached_element_;
  mutable unsigned cached_element_offset_in_array_;
};
DEFINE_TYPE_CASTS(HTMLFormControlsCollection,
                  LiveNodeListBase,
                  collection,
                  collection->GetType() == kFormControls,
                  collection.GetType() == kFormControls);

}  // namespace blink

#endif  // HTMLFormControlsCollection_h
