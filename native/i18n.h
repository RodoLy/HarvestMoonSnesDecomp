/* i18n.h — the native build's own text in Spanish, English and Italian
 * (i18n.c). English is the source: T("...") gives the chosen language's
 * version, or the English when there is none. */
#pragma once

const char *T(const char *english);
void i18n_set(const char *code);        /* "es", "en", "it" */
