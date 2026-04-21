#ifndef SW_EXPORT_H
#define SW_EXPORT_H

#if defined(_WIN32) || defined(__CYGWIN__)
#    if defined(SYPHAX_WEB_SHARED_BUILD)
#        if defined(SYPHAX_WEB_BUILDING_LIBRARY)
#            define SW_API __declspec(dllexport)
#        else
#            define SW_API __declspec(dllimport)
#        endif
#    else
#        define SW_API
#    endif
#else
#    if defined(__GNUC__) && __GNUC__ >= 4
#        define SW_API __attribute__((visibility("default")))
#    else
#        define SW_API
#    endif
#endif

#endif
