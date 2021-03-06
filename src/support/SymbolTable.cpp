#include <cstring>
#include "SymbolTable.h"

//extern "C" uint32_t murmurHash3_32( const uint8_t*, int len, uint32_t seed );
#include "support/murmurHash3.c"
#define hashFunc(str,len)   murmurHash3_32((const uint8_t*)str, len, 0x554956)

const char* SymbolTable::name(symbol_t symbol) const
{
    return &stringStore.front() + index[symbol].stringOffset;
}

symbol_t SymbolTable::intern(const char* name, size_t len)
{
    Entry ent;
    ent.hash = hashFunc(name, len);

    // Check if symbol exists already.
    std::vector<Entry>::iterator it;
    for (it = index.begin(); it != index.end(); ++it) {
        if ((*it).hash == ent.hash)
            return symbol_t(it - index.begin());
    }

    // Not found, so append new symbol.
    ent.stringOffset = stringStore.size();
    stringStore.insert(stringStore.end(), name, name + len + 1);
    index.push_back(ent);
    return symbol_t(index.size() - 1);
}

/**
  Get symbols of the given names.
 
  \param table     Return area for symbols of the names.
  \param count     Number of symbols the table can hold.
  \param names     String containing names separated by whitespace.
*/
void SymbolTable::internSymbols(symbol_t* table, size_t count, const char* names)
{
    const char* cp = names;
    const char* end;
    int c;

    while (*cp) {
        // Skip over whitespace.
        while ((c = *cp)) {
            if(c == ' ' || c == '\t' || c == '\n' || c == '\r')
                ++cp;
            else
                break;
        }

        // Find whitespace.
        end = cp;
        while ((c = *end)) {
            if(c == ' ' || c == '\t' || c == '\n' || c == '\r')
                break;
            ++end;
        }

        if(count-- <= 0)
            break;
        *table++ = intern(cp, end - cp);
        cp = end;
    }
}
