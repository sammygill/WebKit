/**
 * Copyright (C) 2025 Apple Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. AND ITS CONTRIBUTORS ``AS IS''
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL APPLE INC. OR ITS CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */
#include "config.h"

#include <WebCoreTestSupport/GridFormattingContextTester.h>
#include <WebCore/RenderStyleSetters.h>
#include <WebCore/LayoutElementBox.h>

#include <wtf/UniqueRef.h>

namespace TestWebKitAPI {

static UniqueRef<WebCore::Layout::ElementBox> createElementBoxWithOrder(int orderValue)
{
    auto style = WebCore::RenderStyle::create();
    style.setOrder(WebCore::Style::Order { orderValue });
    return makeUniqueRef<WebCore::Layout::ElementBox>(WebCore::Layout::ElementBox::ElementAttributes { }, WTFMove(style));
}

static WebCore::Layout::ElementBox createGridBox()
{
    auto gridStyle = WebCore::RenderStyle::create();
    gridStyle.setDisplay(WebCore::DisplayType::Grid);
    return { { }, WTFMove(gridStyle) };
}

TEST(GridFormattingContext, OrderSimple)
{
    auto gridBox = createGridBox();

    UniqueRef one = createElementBoxWithOrder(1);
    UniqueRef two = createElementBoxWithOrder(2);
    UniqueRef three = createElementBoxWithOrder(3);
    UniqueRef four = createElementBoxWithOrder(4);

    Vector expected { one.ptr(), two.ptr(), three.ptr(), four.ptr() };

    gridBox.insertChild(WTFMove(four));
    gridBox.insertChild(WTFMove(three));
    gridBox.insertChild(WTFMove(one));
    gridBox.insertChild(WTFMove(two));

    WebCore::Layout::GridFormattingContextTester tester(gridBox);

    auto result = tester.testUnplacedGridItemsLogicalOrder();
    EXPECT_EQ(expected, result);
}

TEST(GridFormattingContext, OrderNegative)
{
    auto gridBox = createGridBox();

    UniqueRef one = createElementBoxWithOrder(-4);
    UniqueRef two = createElementBoxWithOrder(-3);
    UniqueRef three = createElementBoxWithOrder(-2);
    UniqueRef four = createElementBoxWithOrder(-1);

    Vector expected { one.ptr(), two.ptr(), three.ptr(), four.ptr() };

    gridBox.insertChild(WTFMove(one));
    gridBox.insertChild(WTFMove(four));
    gridBox.insertChild(WTFMove(three));
    gridBox.insertChild(WTFMove(two));

    WebCore::Layout::GridFormattingContextTester tester(gridBox);

    auto result = tester.testUnplacedGridItemsLogicalOrder();
    EXPECT_EQ(expected, result);
}

TEST(GridFormattingContext, OrderAllSame)
{
    auto gridBox = createGridBox();

    UniqueRef one = createElementBoxWithOrder(5);
    UniqueRef two = createElementBoxWithOrder(5);
    UniqueRef three = createElementBoxWithOrder(5);
    UniqueRef four = createElementBoxWithOrder(5);

    Vector expected { one.ptr(), two.ptr(), three.ptr(), four.ptr() };

    gridBox.appendChild(WTFMove(one));
    gridBox.appendChild(WTFMove(two));
    gridBox.appendChild(WTFMove(three));
    gridBox.appendChild(WTFMove(four));

    WebCore::Layout::GridFormattingContextTester tester(gridBox);

    auto result = tester.testUnplacedGridItemsLogicalOrder();
    
    EXPECT_EQ(expected, result);
}

}
