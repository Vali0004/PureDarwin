/*
 * These six legacy constant names are declared CF_EXPORT in the public
 * CFLocale.h/CFDateFormatter.h headers in this same vendored source tree,
 * but are never actually defined anywhere in the vendored .c files -
 * apparently dropped in this swift-corelibs-foundation port while the
 * "...Key" suffixed replacements they alias were kept. Call sites elsewhere
 * in CF (CFDateIntervalFormatter.c, CFCalendar_Enumerate.c, CFDateFormatter.c)
 * confirm the intended values by usage: dictionary-key roles matching the
 * "...Key" constants, and a calendar-identifier role for kCFGregorianCalendar
 * matching kCFCalendarIdentifierGregorian.
 *
 * A plain "const CFStringRef kCFLocaleCalendar = kCFLocaleCalendarKey;"
 * isn't a valid C static initializer here: kCFLocaleCalendarKey is itself
 * an extern global, and copying another global's *value* (as opposed to
 * taking its address) isn't a constant expression, so this has to run as
 * load-time initialization instead.
 */

#include <CoreFoundation/CFLocale.h>
#include <CoreFoundation/CFDateFormatter.h>
#include <CoreFoundation/CFCalendar.h>
#include "CFLocaleInternal.h"

const CFLocaleKey kCFLocaleCalendar;
const CFLocaleKey kCFLocaleLanguageCode;
const CFLocaleKey kCFLocaleScriptCode;
const CFDateFormatterKey kCFDateFormatterCalendar;
const CFDateFormatterKey kCFDateFormatterTimeZone;
const CFCalendarIdentifier kCFGregorianCalendar;

__attribute__((constructor))
static void __CFLocalePureDarwinLegacyAliasesInit(void) {
    *(CFStringRef *)&kCFLocaleCalendar = kCFLocaleCalendarKey;
    *(CFStringRef *)&kCFLocaleLanguageCode = kCFLocaleLanguageCodeKey;
    *(CFStringRef *)&kCFLocaleScriptCode = kCFLocaleScriptCodeKey;
    *(CFStringRef *)&kCFDateFormatterCalendar = kCFDateFormatterCalendarKey;
    *(CFStringRef *)&kCFDateFormatterTimeZone = kCFDateFormatterTimeZoneKey;
    *(CFStringRef *)&kCFGregorianCalendar = kCFCalendarIdentifierGregorian;
}
