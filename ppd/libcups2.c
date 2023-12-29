//
// Wrapper function for ipp.c for libcups2.
//
// Copyright 2020-2022 by Till Kamppeter.
//
// Licensed under Apache License v2.0.  See the file "LICENSE" for more
// information.
//
//
// Include necessary headers...
//


#include <config.h>

#ifdef HAVE_LIBCUPS2

#include <ppd/libcups2-private.h>
#include "string-private.h"
#include "thread-private.h"
#include "debug-internal.h"
#ifdef HAVE_LANGINFO_H
#  include <langinfo.h>
#endif // HAVE_LANGINFO_H
#ifdef _WIN32
#  include <io.h>
#else
#  include <unistd.h>
#endif // _WIN32
#ifdef HAVE_COREFOUNDATION_H
#  include <CoreFoundation/CoreFoundation.h>
#endif // HAVE_COREFOUNDATION_H
#include <cups/file.h>
#include <cups/http.h>
#include <signal.h>
#include <unistd.h>
#include <errno.h>
#include <sys/wait.h>


//
// Constants...
//

#  define _PPD_MESSAGE_PO	0	// Message file is in GNU .po format
#  define _PPD_MESSAGE_UNQUOTE	1	// Unescape \foo in strings?
#  define _PPD_MESSAGE_STRINGS	2	// Message file is in Apple .strings
					// format
#  define _PPD_MESSAGE_EMPTY	4	// Allow empty localized strings


//
// Types...
//

typedef struct _ppd_message_s		// **** Message catalog entry ****
{
  char	*msg,				// Original string
	*str;				// Localized string
} _ppd_message_t;


//
// Local globals...
//

static _ppd_mutex_t	lang_mutex = _PPD_MUTEX_INITIALIZER;
					// Mutex to control access to cache


//
// Local functions...
//

//
// 'ppd_message_compare()' - Compare two messages.
//

static int			// O - Result of comparison
ppd_message_compare(
    _ppd_message_t *m1,		// I - First message
    _ppd_message_t *m2)		// I - Second message
{
  return (strcmp(m1->msg, m2->msg));
}


//
// 'ppd_message_free()' - Free a message.
//

static void
ppd_message_free(_ppd_message_t *m)	// I - Message
{
  if (m->msg)
    free(m->msg);

  if (m->str)
    free(m->str);

  free(m);
}


//
// 'ppd_unquote()' - Unquote characters in strings...
//

static void
ppd_unquote(char       *d,		// O - Unquoted string
             const char *s)		// I - Original string
{
  while (*s)
  {
    if (*s == '\\')
    {
      s ++;
      if (isdigit(*s))
      {
	*d = 0;

	while (isdigit(*s))
	{
	  *d = *d * 8 + *s - '0';
	  s ++;
	}

	d ++;
      }
      else
      {
	if (*s == 'n')
	  *d ++ = '\n';
	else if (*s == 'r')
	  *d ++ = '\r';
	else if (*s == 't')
	  *d ++ = '\t';
	else
	  *d++ = *s;

	s ++;
      }
    }
    else
      *d++ = *s++;
  }

  *d = '\0';
}


//
// 'ppd_read_strings()' - Read a pair of strings from a .strings file.
//

static int				// O - 1 on success, 0 on failure
ppd_read_strings(cups_file_t  *fp,	// I - .strings file
                 int          flags,	// I - CUPS_MESSAGE_xxx flags
		 cups_array_t *a)	// I - Message catalog array
{
  char			buffer[8192],	// Line buffer
			*bufptr,	// Pointer into buffer
			*msg,		// Pointer to start of message
			*str;		// Pointer to start of translation string
  _ppd_message_t	*m;		// New message


  while (cupsFileGets(fp, buffer, sizeof(buffer)))
  {
    //
    // Skip any line (comments, blanks, etc.) that isn't:
    //
    //   "message" = "translation";
    //

    for (bufptr = buffer; *bufptr && isspace(*bufptr & 255); bufptr ++);

    if (*bufptr != '\"')
      continue;

    //
    // Find the end of the message...
    //

    bufptr ++;
    for (msg = bufptr; *bufptr && *bufptr != '\"'; bufptr ++)
      if (*bufptr == '\\' && bufptr[1])
        bufptr ++;

    if (!*bufptr)
      continue;

    *bufptr++ = '\0';

    if (flags & _PPD_MESSAGE_UNQUOTE)
      ppd_unquote(msg, msg);

    //
    // Find the start of the translation...
    //

    while (*bufptr && isspace(*bufptr & 255))
      bufptr ++;

    if (*bufptr != '=')
      continue;

    bufptr ++;
    while (*bufptr && isspace(*bufptr & 255))
      bufptr ++;

    if (*bufptr != '\"')
      continue;

    //
    // Find the end of the translation...
    //

    bufptr ++;
    for (str = bufptr; *bufptr && *bufptr != '\"'; bufptr ++)
      if (*bufptr == '\\' && bufptr[1])
        bufptr ++;

    if (!*bufptr)
      continue;

    *bufptr++ = '\0';

    if (flags & _PPD_MESSAGE_UNQUOTE)
      ppd_unquote(str, str);

    //
    // If we get this far we have a valid pair of strings, add them...
    //

    if ((m = malloc(sizeof(_ppd_message_t))) == NULL)
      break;

    m->msg = strdup(msg);
    m->str = strdup(str);

    if (m->msg && m->str)
      cupsArrayAdd(a, m);
    else
    {
      if (m->msg)
	free(m->msg);

      if (m->str)
	free(m->str);

      free(m);
      break;
    }

    return (1);
  }

  //
  // No more strings...
  //

  return (0);
}


//
// 'ppd_message_file_load()' - Load a .po or .strings file into a messages
//                             array.
//

static cups_array_t *				// O - New message array
ppd_message_file_load(const char *filename,	// I - Message catalog to load
                 int        flags)	// I - Load flags
{
  cups_file_t		*fp;		// Message file
  cups_array_t		*a;		// Message array
  _ppd_message_t	*m;		// Current message
  char			s[4096],	// String buffer
			*ptr,		// Pointer into buffer
			*temp;		// New string
  size_t		length,		// Length of combined strings
			ptrlen;		// Length of string


  DEBUG_printf(("4ppd_message_file_load(filename=\"%s\")", filename));

  //
  // Create an array to hold the messages...
  //

  if ((a = cupsArrayNew3((cups_array_cb_t)ppd_message_compare, NULL,
                         (cups_ahash_cb_t)NULL, 0,
                         (cups_acopy_cb_t)NULL,
                         (cups_afree_cb_t)ppd_message_free)) == NULL)
  {
    DEBUG_puts("5ppd_message_file_load: Unable to allocate array!");
    return (NULL);
  }

  //
  // Open the message catalog file...
  //

  if ((fp = cupsFileOpen(filename, "r")) == NULL)
  {
    DEBUG_printf(("5ppd_message_file_load: Unable to open file: %s",
                  strerror(errno)));
    return (a);
  }

  if (flags & _PPD_MESSAGE_STRINGS)
  {
    while (ppd_read_strings(fp, flags, a));
  }
  else
  {
    //
    // Read messages from the catalog file until EOF...
    //
    // The format is the GNU gettext .po format, which is fairly simple:
    //
    //     msgid "some text"
    //     msgstr "localized text"
    //
    // The ID and localized text can span multiple lines using the form:
    //
    //     msgid ""
    //     "some long text"
    //     msgstr ""
    //     "localized text spanning "
    //     "multiple lines"
    //

    m = NULL;

    while (cupsFileGets(fp, s, sizeof(s)) != NULL)
    {
      //
      // Skip blank and comment lines...
      //

      if (s[0] == '#' || !s[0])
	continue;

      //
      // Strip the trailing quote...
      //

      if ((ptr = strrchr(s, '\"')) == NULL)
	continue;

      *ptr = '\0';

      //
      // Find start of value...
      //

      if ((ptr = strchr(s, '\"')) == NULL)
	continue;

      ptr ++;

      //
      // Unquote the text...
      //

      if (flags & _PPD_MESSAGE_UNQUOTE)
	ppd_unquote(ptr, ptr);

      //
      // Create or add to a message...
      //

      if (!strncmp(s, "msgid", 5))
      {
	//
	// Add previous message as needed...
	//

	if (m)
	{
	  if (m->str && (m->str[0] || (flags & _PPD_MESSAGE_EMPTY)))
	    cupsArrayAdd(a, m);
	  else
	  {
	    //
	    // Translation is empty, don't add it... (STR #4033)
	    //

	    free(m->msg);
	    if (m->str)
	      free(m->str);
	    free(m);
	  }
	}

	//
	// Create a new message with the given msgid string...
	//

	if ((m = (_ppd_message_t *)calloc(1, sizeof(_ppd_message_t))) == NULL)
	  break;

	if ((m->msg = strdup(ptr)) == NULL)
	{
	  free(m);
	  m = NULL;
	  break;
	}
      }
      else if (s[0] == '\"' && m)
      {
	//
	// Append to current string...
	//

	length = strlen(m->str ? m->str : m->msg);
	ptrlen = strlen(ptr);

	if ((temp = realloc(m->str ? m->str : m->msg,
			    length + ptrlen + 1)) == NULL)
	{
	  if (m->str)
	    free(m->str);
	  free(m->msg);
	  free(m);
	  m = NULL;
	  break;
	}

	if (m->str)
	{
	  //
	  // Copy the new portion to the end of the msgstr string - safe
	  // to use memcpy because the buffer is allocated to the correct
	  // size...
	  //

	  m->str = temp;

	  memcpy(m->str + length, ptr, ptrlen + 1);
	}
	else
	{
	  //
	  // Copy the new portion to the end of the msgid string - safe
	  // to use memcpy because the buffer is allocated to the correct
	  // size...
	  //

	  m->msg = temp;

	  memcpy(m->msg + length, ptr, ptrlen + 1);
	}
      }
      else if (!strncmp(s, "msgstr", 6) && m)
      {
	//
	// Set the string...
	//

	if ((m->str = strdup(ptr)) == NULL)
	{
	  free(m->msg);
	  free(m);
	  m = NULL;
          break;
	}
      }
    }

    //
    // Add the last message string to the array as needed...
    //

    if (m)
    {
      if (m->str && (m->str[0] || (flags & _PPD_MESSAGE_EMPTY)))
	cupsArrayAdd(a, m);
      else
      {
	//
	// Translation is empty, don't add it... (STR #4033)
	//

	free(m->msg);
	if (m->str)
	  free(m->str);
	free(m);
      }
    }
  }

  //
  // Close the message catalog file and return the new array...
  //

  cupsFileClose(fp);

  DEBUG_printf(("5ppd_message_file_load: Returning %d messages...",
		cupsArrayGetCount(a)));

  return (a);
}


//
// 'ppd_message_lookup()' - Lookup a message string.
//

static const char *			// O - Localized message
ppd_message_lookup(cups_array_t *a,	// I - Message array
                   const char   *m)	// I - Message
{
  _ppd_message_t	key,		// Search key
			*match;		// Matching message


  DEBUG_printf(("ppd_message_lookup(a=%p, m=\"%s\")", (void *)a, m));

  //
  // Lookup the message string; if it doesn't exist in the catalog,
  // then return the message that was passed to us...
  //

  key.msg = (char *)m;
  match   = (_ppd_message_t *)cupsArrayFind(a, &key);

#if defined(__APPLE__) && defined(CUPS_BUNDLEDIR)
  if (!match && cupsArrayUserData(a))
  {
    //
    // Try looking the string up in the cups.strings dictionary...
    //

    CFDictionaryRef	dict;		// cups.strings dictionary
    CFStringRef		cfm,		// Message as a CF string
			cfstr;		// Localized text as a CF string

    dict       = (CFDictionaryRef)cupsArrayUserData(a);
    cfm        = CFStringCreateWithCString(kCFAllocatorDefault, m,
					   kCFStringEncodingUTF8);
    match      = calloc(1, sizeof(_ppd_message_t));
    match->msg = strdup(m);
    cfstr      = cfm ? CFDictionaryGetValue(dict, cfm) : NULL;

    if (cfstr)
    {
      char	buffer[1024];		// Message buffer

      CFStringGetCString(cfstr, buffer, sizeof(buffer), kCFStringEncodingUTF8);
      match->str = strdup(buffer);

      DEBUG_printf(("1ppd_message_lookup: Found \"%s\" as \"%s\"...",
		    m, buffer));
    }
    else
    {
      match->str = strdup(m);

      DEBUG_printf(("1ppd_message_lookup: Did not find \"%s\"...", m));
    }

    cupsArrayAdd(a, match);

    if (cfm)
      CFRelease(cfm);
  }
#endif // __APPLE__ && CUPS_BUNDLEDIR

  if (match && match->str)
    return (match->str);
  else
    return (m);
}


//
// 'ppd_message_load()' - Load the message catalog for a language.
//

static void
ppd_message_load(cups_lang_t *lang)	// I - Language
{
  char			filename[1024];	// Filename for language locale file
  char			localedir[1003];// Filename for language locale dir
  char			*p;

  // Directory supplied by environment variable CUPS_LOCALEDIR
  if ((p = getenv("CUPS_LOCALEDIR")) != NULL)
    strncpy (localedir, p, sizeof(localedir) - 1);
  else
  {
    // Determine CUPS datadir (usually /usr/share/cups)
    if ((p = getenv("CUPS_DATADIR")) == NULL)
      p = CUPS_DATADIR;
    snprintf(localedir, sizeof(localedir), "%s/locale", p);
  }

  snprintf(filename, sizeof(filename), "%s/%.5s/cups_%.5s.po", localedir,
	   cupsLangGetName(lang), cupsLangGetName(lang));

  if (strchr(cupsLangGetName(lang), '_') && access(filename, 0))
  {
    //
    // Country localization not available, look for generic localization...
    //

    snprintf(filename, sizeof(filename), "%s/%.2s/cups_%.2s.po", localedir,
             cupsLangGetName(lang), cupsLangGetName(lang));

    if (access(filename, 0))
    {
      //
      // No generic localization, so use POSIX...
      //

      DEBUG_printf(("4ppd_message_load: access(\"%s\", 0): %s", filename,
                    strerror(errno)));

      snprintf(filename, sizeof(filename), "%s/C/cups_C.po", localedir);
    }
  }

  //
  // Read the strings from the file...
  //

  lang->strings = ppd_message_file_load(filename, _PPD_MESSAGE_UNQUOTE);
}


//
// libcups2 equivalents of libcups3 functions
//

//
// 'cupsLangGetName()' - Get the language name.
//
// Replaces lang->language
//

const char *                            // O - Language name
cupsLangGetName(cups_lang_t *lang)      // I - Language data
{
  return (lang ? lang->language : NULL);
}

//
// 'cupsLangGetString()' - Get a message string.
//
// The returned string is UTF-8 encoded; use cupsUTF8ToCharset() to
// convert the string to the language encoding.
//

const char *				// O - Localized message
cupsLangGetString(cups_lang_t *lang,	// I - Language
                  const char  *message)	// I - Message
{
  const char *s;			// Localized message


  DEBUG_printf(("cupsLangGetString(lang=%p, message=\"%s\")",
		(void *)lang, message));

  //
  // Range check input...
  //

  if (!lang || !message || !*message)
    return (message);

  _ppdMutexLock(&lang_mutex);

  //
  // Load the message catalog if needed...
  //

  if (!lang->strings)
    ppd_message_load(lang);

  s = ppd_message_lookup(lang->strings, message);

  _ppdMutexUnlock(&lang_mutex);

  return (s);
}


#endif // HAVE_LIBCUPS2
