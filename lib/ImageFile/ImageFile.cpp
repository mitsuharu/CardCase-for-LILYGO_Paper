#include "ImageFile.h"

namespace
{
    char toLowerAscii(char c)
    {
        return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
    }

    /// name が suffix で終わるか（大文字小文字を無視）
    bool endsWithIgnoreCase(const String &name, const char *suffix)
    {
        size_t nameLength = name.length();

        size_t suffixLength = 0;
        while (suffix[suffixLength] != '\0')
        {
            suffixLength++;
        }

        if (suffixLength == 0 || nameLength < suffixLength)
        {
            return false;
        }

        size_t offset = nameLength - suffixLength;
        for (size_t i = 0; i < suffixLength; i++)
        {
            if (toLowerAscii(name[offset + i]) != toLowerAscii(suffix[i]))
            {
                return false;
            }
        }
        return true;
    }
}

namespace ImageFile
{
    bool isSupportedImage(const String &name)
    {
        return endsWithIgnoreCase(name, ".jpg") || endsWithIgnoreCase(name, ".jpeg") || endsWithIgnoreCase(name, ".png");
    }

    bool isHidden(const String &name)
    {
        return name.length() > 0 && name[0] == '.';
    }

    bool isListable(const String &name)
    {
        return !isHidden(name) && isSupportedImage(name);
    }

    String rootPath(const String &name)
    {
        if (name.length() > 0 && name[0] == '/')
        {
            return name;
        }
        return String("/") + name;
    }
}
