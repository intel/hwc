/*
// Copyright (c) 2017 Intel Corporation
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
*/

#ifndef INTEL_UFO_HWC_HWCLIST_H
#define INTEL_UFO_HWC_HWCLIST_H

namespace intel {
namespace ufo {
namespace hwc {

// This class is a specialisation for managing lists of objects, used in a variety of situations as an alternative to vectors
// In particular, it minimises reallocations by assigning elements to an unused list
template <class T>
class HwcList
{
public:
    HwcList() :
        mpElement(NULL),
        mSize(0)
    {
    }

    uint32_t size() const
    {
        return mSize;
    }

    bool isEmpty() const
    {
        return 0 == mSize;
    }

    // Empty the layer list
    void clear()
    {
        while (mpElement)
        {
            Element* pNext = pop_front();
            sUnusedElements.push_front(pNext);
        }
    }

    bool grow(uint32_t newSize)
    {
        while (mSize < newSize)
        {
            Element* pNew;
            if (sUnusedElements.size() == 0)
            {
                // Need to allocate a new element
                pNew = new(std::nothrow) Element();
                if (pNew == NULL)
                {
                    // Failure case, caller must handle if they care
                    return false;
                }
            }
            else
            {
                // Just take the element from the front of the unused list
                pNew = sUnusedElements.pop_front();
                pNew->clear();
            }
            push_back(pNew);
        }
        return true;
    }

    bool shrink(uint32_t newSize)
    {
        // Shrink if needed
        while (mSize > newSize)
        {
            Element* pOld = pop_front();
            sUnusedElements.push_front(pOld);
        }

        // Make sure this list doesnt keep on growing
        while (sUnusedElements.size() > cMaxUnusedElements)
        {
            Element* pOld = sUnusedElements.pop_front();
            delete pOld;
        }
        return true;
    }

    // Resize the layer list obtaining any additional elements from or returning any unneeded elements to the source list
    // Note, the elements added will have stale contents. Make sure the caller initialises everything appropriately
    bool resize(uint32_t newSize)
    {
        if (mSize < newSize)
        {
            // Grow if needed
            return grow(newSize);

        }
        else if (mSize > newSize)
        {
            return shrink(newSize);
        }
        return true;
    }

    T& operator[](uint32_t index)
    {
        Element* pLayer = mpElement;
        ALOG_ASSERT(index < mSize);
        while (index)
        {
            ALOG_ASSERT(pLayer);
            pLayer = pLayer->mpNext;
            index--;
        }
        return pLayer->mElement;
    }

    const T& operator[](uint32_t index) const
    {
        const Element* pLayer = mpElement;
        ALOG_ASSERT(index < mSize);
        while (index)
        {
            ALOG_ASSERT(pLayer);
            pLayer = pLayer->mpNext;
            index--;
        }
        return pLayer->mElement;
    }

private:
    // Helper internal class
    class Element
    {
    public:
        Element() : mpNext(NULL) {}

        void clear() { mElement.clear(); }
        T           mElement;
        Element*    mpNext;
    };

    // Helper to remove the requested element from the front of the list
    Element* pop_front()
    {
        ALOG_ASSERT(mpElement && mSize);
        Element* pElement = mpElement;
        mpElement = mpElement->mpNext;
        mSize--;
        return pElement;
    }

    // Helper to add requested element to the front of the list
    void push_front(Element* pElement)
    {
        ALOG_ASSERT(pElement);
        pElement->mpNext = mpElement;
        mpElement = pElement;
        mSize++;
    }

    // Helper to add requested element to the back of the list
    void push_back(Element* pElement)
    {
        Element **ppElement = &mpElement;
        while (*ppElement)
            ppElement = &((*ppElement)->mpNext);
        *ppElement = pElement;
        pElement->mpNext = NULL;
        mSize++;
    }

private:
    const static uint32_t cMaxUnusedElements = 64;
    static HwcList<T> sUnusedElements;
    Element*        mpElement;
    uint32_t        mSize;
};

template <class T> HwcList<T> HwcList<T>::sUnusedElements;

}; // namespace hwc
}; // namespace ufo
}; // namespace intel

#endif // INTEL_UFO_HWC_HWCLIST_H
