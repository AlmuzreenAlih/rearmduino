#pragma once

#include <Qsci/qscilexercpp.h>

// C++ lexer with keywords split so types can be colored separately:
// set 1 = keywords, set 2 = types (QsciLexerCPP::KeywordSet2).
class ArduinoLexer : public QsciLexerCPP
{
public:
    using QsciLexerCPP::QsciLexerCPP;

    const char *keywords(int set) const override
    {
        if (set == 1)
            return "alignas alignof and asm auto break case catch class const constexpr "
                   "const_cast continue decltype default delete do dynamic_cast else enum "
                   "explicit export extern false for friend goto if inline mutable namespace "
                   "new noexcept not nullptr operator or private protected public register "
                   "reinterpret_cast return sizeof static static_assert static_cast struct "
                   "switch template this throw true try typedef typeid typename union using "
                   "virtual volatile while xor";
        if (set == 2)
            return "bool boolean byte char char16_t char32_t double float int long short "
                   "signed unsigned void wchar_t word size_t String "
                   "int8_t uint8_t int16_t uint16_t int32_t uint32_t int64_t uint64_t";
        return QsciLexerCPP::keywords(set);
    }
};
