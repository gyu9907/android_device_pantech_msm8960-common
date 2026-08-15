/*
 * Copyright (C) 2026 The LineageOS Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <SkCanvas.h>
#include <SkPaint.h>
#include <SkRect.h>
#include <unicode/uchar.h>
#include <unicode/utext.h>
#include <unicode/utypes.h>

extern "C" void drawRectCoords(SkCanvas* canvas, SkScalar left, SkScalar top,
        SkScalar right, SkScalar bottom, const SkPaint& paint)
        __asm__("_ZN8SkCanvas14drawRectCoordsEffffRK7SkPaint");

extern "C" void drawRectCoords(SkCanvas* canvas, SkScalar left, SkScalar top,
        SkScalar right, SkScalar bottom, const SkPaint& paint) {
    canvas->drawRect(SkRect::MakeLTRB(left, top, right, bottom), paint);
}

extern "C" int32_t uDigit56(UChar32 ch, int8_t radix) __asm__("u_digit_56");
extern "C" int32_t uDigit56(UChar32 ch, int8_t radix) {
    return u_digit(ch, radix);
}

extern "C" const char* uErrorName56(UErrorCode code) __asm__("u_errorName_56");
extern "C" const char* uErrorName56(UErrorCode code) {
    return u_errorName(code);
}

extern "C" UText* uTextClose56(UText* text) __asm__("utext_close_56");
extern "C" UText* uTextClose56(UText* text) {
    return utext_close(text);
}

extern "C" UText* uTextOpenUChars56(UText* text, const UChar* source,
        int64_t length, UErrorCode* status) __asm__("utext_openUChars_56");
extern "C" UText* uTextOpenUChars56(UText* text, const UChar* source,
        int64_t length, UErrorCode* status) {
    return utext_openUChars(text, source, length, status);
}
