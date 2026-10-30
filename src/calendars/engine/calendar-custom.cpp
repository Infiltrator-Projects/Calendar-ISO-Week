// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 1993-2026 Shannon Smith

/*
 * Private non-ICU chronology descriptors.
 *
 * Every custom calendar is described by one immutable operation table.  The
 * resource ABI adapter never needs to know whether a period is a Julian
 * month, ISO week, Maya uinal or Badíʿ month: it asks the provider to
 * convert JDN <-> fields, format the fields and navigate periods.
 *
 * The table is intentionally data-driven.  Adding a new custom calendar means
 * supplying its small set of operations and registering one descriptor; it
 * does not require extending a collection of central switch statements.
 *
 * Navigation preserves the displayed day when possible and clamps it only
 * when the destination period is shorter.  Fixed-size period calendars can
 * bypass field conversion entirely, which keeps ISO-week and Mayan navigation
 * both simpler and less error-prone.
 */

#include "calendar-custom.h"
#include "calendar-ancient.h"
#include "calendar-bahai.h"
#include "calendar-helpers.h"
#include "calendar-historical.h"
#include "calendar-perpetual.h"
#include "calendar-reform.h"
#include "julian-day.h"

#include <glib/gi18n-lib.h>

/* The generic engine tests compile all descriptor families. Each standalone
 * resource compiles only its own family, with unrelated helpers excluded by
 * the same preprocessor selection as its descriptor. No warning suppression
 * is needed, and the compiler can still detect accidental unused functions. */

typedef CalendarCalendarMode CalendarMode;
typedef CalendarCalendarFields CalendarFields;

typedef void (*FieldsFromJdnFunc)(gint64 jdn, CalendarFields *fields);
typedef gint64 (*FieldsToJdnFunc)(const CalendarFields *fields);
typedef gint (*MonthLengthFunc)(const CalendarFields *fields);
typedef gchar *(*FormatFunc)(const CalendarFields *fields,
                             CalendarDatePart part);
typedef gint (*PeriodIndexFunc)(gint month);
typedef gint (*MonthFromPeriodFunc)(gint period);

typedef struct
{
    FieldsFromJdnFunc fields_from_jdn;
    FieldsToJdnFunc fields_to_jdn;
    MonthLengthFunc month_length;
    FormatFunc format;
    PeriodIndexFunc period_index;
    MonthFromPeriodFunc month_from_period;
    gint periods_per_year;
    gint fixed_period_days;
    gint fixed_year_days;
    gint period_origin_day;
    gboolean clamp_iso_weeks;
} CustomCalendarProvider;

#define floor_divide calendar_floor_divide
#define positive_modulo calendar_positive_modulo
#define gregorian_is_leap calendar_gregorian_is_leap
#define gregorian_to_jdn calendar_gregorian_to_jdn
#define jdn_to_gregorian calendar_jdn_to_gregorian
#define julian_to_jdn calendar_julian_to_jdn
#define jdn_to_julian calendar_jdn_to_julian
#define gregorian_day_of_year calendar_gregorian_day_of_year
#define iso_weekday calendar_iso_weekday

static gint
iso_weeks_in_year(gint64 year)
{
    const gint january_first = iso_weekday(gregorian_to_jdn(year, 1, 1));

    return january_first == 4 ||
           (january_first == 3 && gregorian_is_leap(year)) ? 53 : 52;
}

static void
iso_fields_from_jdn(gint64 jdn,
                    CalendarFields *fields)
{
    gint gregorian_year;
    gint month;
    gint day;
    gint week;
    gint64 year;
    const gint weekday = iso_weekday(jdn);

    jdn_to_gregorian(jdn, &gregorian_year, &month, &day);
    year = gregorian_year;
    week = (gregorian_day_of_year(gregorian_year, month, day) -
            weekday + 10) / 7;

    if (week < 1)
    {
        year = calendar_i64_subtract_saturating(year, 1);
        week = iso_weeks_in_year(year);
    }
    else if (week > iso_weeks_in_year(year))
    {
        year = calendar_i64_add_saturating(year, 1);
        week = 1;
    }

    *fields = CalendarFields{
        .year = year,
        .month = week,
        .day = weekday,
        .auxiliary = 0,
        .special = FALSE
    };
}

static gint64
iso_fields_to_jdn(const CalendarFields *fields)
{
    const gint64 january_fourth =
        gregorian_to_jdn(fields->year, 1, 4);
    const gint64 week_one_monday =
        january_fourth - (iso_weekday(january_fourth) - 1);

    return week_one_monday +
           (gint64)(fields->month - 1) * CALENDAR_DAYS_PER_WEEK +
           fields->day - 1;
}

static gint
iso_month_length(const CalendarFields *fields)
{
    (void)fields;
    return 7;
}

static gint
default_period_index(gint month)
{
    return month - 1;
}

static gint
default_month_from_period(gint period)
{
    return period + 1;
}

static gchar *
format_iso(const CalendarFields *fields,
           CalendarDatePart part)
{
    if (part == CALENDAR_DATE_PART_DAY)
        return g_strdup_printf("%d", fields->day);
    if (part == CALENDAR_DATE_PART_MONTH)
        return g_strdup_printf(_("Week %02d"), fields->month);
    if (part == CALENDAR_DATE_PART_YEAR)
        return g_strdup_printf("%" G_GINT64_FORMAT, fields->year);

    return g_strdup_printf("%04" G_GINT64_FORMAT "-W%02d-%d",
                           fields->year,
                           fields->month,
                           fields->day);
}

/*
 * Native C++20 compile-time descriptor for this single independently installed
 * chronology. No sparse C array or copy of Common's generic registry.
 */
static const CustomCalendarProvider iso_provider = {
    iso_fields_from_jdn, iso_fields_to_jdn, iso_month_length,
    format_iso, default_period_index, default_month_from_period,
    0, 7, 0, 1, TRUE
};
static const CustomCalendarProvider *custom_provider(CalendarMode mode)
{
    return mode == CALENDAR_CALENDAR_MODE_ISO_WEEK ? &iso_provider : nullptr;
}

void
calendar_custom_fields_from_jdn(CalendarMode mode,
                                     gint64 jdn,
                                     CalendarFields *fields)
{
    (void)mode;
    const CustomCalendarProvider *provider = custom_provider(mode);

    g_return_if_fail(fields != NULL);
    *fields = CalendarFields{};

    if (provider != NULL)
        provider->fields_from_jdn(jdn, fields);
}

gchar *
calendar_custom_format(CalendarMode mode,
                            const CalendarFields *fields,
                            CalendarDatePart part)
{
    (void)mode;
    const CustomCalendarProvider *provider = custom_provider(mode);

    if (provider == NULL || fields == NULL || provider->format == NULL)
        return g_strdup("");

    return provider->format(fields, part);
}

gint64
calendar_custom_month_start(CalendarMode mode,
                                 gint64 jdn)
{
    (void)mode;
    const CustomCalendarProvider *provider = custom_provider(mode);
    CalendarFields fields;

    if (provider == NULL)
        return CALENDAR_UNIX_EPOCH_JDN;

    provider->fields_from_jdn(jdn, &fields);
    ;
    fields.day = provider->period_origin_day;
    return provider->fields_to_jdn(&fields);
}

gint64
calendar_custom_add_months(CalendarMode mode,
                                gint64 jdn,
                                gint amount)
{
    (void)mode;
    const CustomCalendarProvider *provider = custom_provider(mode);
    CalendarFields fields;
    gint64 serial;
    gint period;

    if (provider == NULL)
        return jdn;

    return calendar_i64_add_saturating(
            jdn,
            calendar_i64_multiply_saturating(
                amount, provider->fixed_period_days));

    provider->fields_from_jdn(jdn, &fields);
    ;
    period = provider->period_index(fields.month);
    serial = calendar_i64_add_saturating(
        calendar_i64_add_saturating(
            calendar_i64_multiply_saturating(
                fields.year, provider->periods_per_year),
            period),
        amount);
    fields.year = floor_divide(serial, provider->periods_per_year);
    period = (gint)positive_modulo(serial, provider->periods_per_year);
    fields.month = provider->month_from_period(period);
    fields.day = MIN(fields.day, provider->month_length(&fields));

    return provider->fields_to_jdn(&fields);
}

gint64
calendar_custom_add_years(CalendarMode mode,
                               gint64 jdn,
                               gint amount)
{
    (void)mode;
    const CustomCalendarProvider *provider = custom_provider(mode);
    CalendarFields fields;

    if (provider == NULL)
        return jdn;

    ;

    provider->fields_from_jdn(jdn, &fields);
    ;
    fields.year = calendar_i64_add_saturating(fields.year, amount);
    fields.month = MIN(fields.month, iso_weeks_in_year(fields.year));
    fields.day = MIN(fields.day, provider->month_length(&fields));

    return provider->fields_to_jdn(&fields);
}


