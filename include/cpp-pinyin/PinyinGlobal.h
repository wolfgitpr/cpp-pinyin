#ifndef PINYINGLOBAL_H
#define PINYINGLOBAL_H

#ifdef _MSC_VER
#  define CPP_PINYIN_DECL_EXPORT __declspec(dllexport)
#  define CPP_PINYIN_DECL_IMPORT __declspec(dllimport)
#else
#  define CPP_PINYIN_DECL_EXPORT __attribute__((visibility("default")))
#  define CPP_PINYIN_DECL_IMPORT __attribute__((visibility("default")))
#endif

#ifndef CPP_PINYIN_EXPORT
#  ifdef CPP_PINYIN_STATIC
#    define CPP_PINYIN_EXPORT
#  else
#    ifdef CPP_PINYIN_LIBRARY
#      define CPP_PINYIN_EXPORT CPP_PINYIN_DECL_EXPORT
#    else
#      define CPP_PINYIN_EXPORT CPP_PINYIN_DECL_IMPORT
#    endif
#  endif
#endif

#endif //PINYINGLOBAL_H
