#include "platform/display/cyberdeck_clock.h"

#include <stdio.h>

namespace {

bool leap_year(int year)
{
    return (year % 4 == 0) && ((year % 100 != 0) || (year % 400 == 0));
}

int days_in_month(int year, int month)
{
    static const int days[] = {31, 28, 31, 30, 31, 30,
                               31, 31, 30, 31, 30, 31};
    return days[month - 1] + (month == 2 && leap_year(year) ? 1 : 0);
}

bool valid_time(const cyberdeck_clock_time_t *value)
{
    if (value == nullptr || value->year < 1 || value->year > 9999 ||
        value->month < 1 || value->month > 12 || value->day < 1 ||
        value->day > days_in_month(value->year, value->month) ||
        value->hour > 23 || value->minute > 59) {
        return false;
    }
    return true;
}

int64_t days_before_year(int year)
{
    const int64_t y = static_cast<int64_t>(year) - 1;
    return y * 365 + y / 4 - y / 100 + y / 400;
}

int64_t days_before_date(const cyberdeck_clock_time_t *value)
{
    int64_t days = days_before_year(value->year);
    for (int month = 1; month < value->month; ++month) {
        days += days_in_month(value->year, month);
    }
    return days + value->day - 1;
}

} // namespace

extern "C" bool cyberdeck_clock_from_utc(const cyberdeck_clock_time_t *utc,
                                          int32_t offset_minutes,
                                          cyberdeck_clock_time_t *out)
{
    if (!valid_time(utc) || out == nullptr) {
        return false;
    }

    const int64_t total_minutes = days_before_date(utc) * 1440 +
                                  static_cast<int64_t>(utc->hour) * 60 +
                                  utc->minute + offset_minutes;
    int64_t days = total_minutes / 1440;
    int64_t minute_of_day = total_minutes % 1440;
    if (minute_of_day < 0) {
        minute_of_day += 1440;
        --days;
    }

    if (days < 0) {
        return false;
    }

    int year = 1;
    while (year <= 9999) {
        const int64_t year_days = leap_year(year) ? 366 : 365;
        if (days < year_days) {
            break;
        }
        days -= year_days;
        ++year;
    }
    if (year > 9999) {
        return false;
    }

    int month = 1;
    while (days >= days_in_month(year, month)) {
        days -= days_in_month(year, month);
        ++month;
    }

    cyberdeck_clock_time_t result = {
        static_cast<int16_t>(year), static_cast<uint8_t>(month),
        static_cast<uint8_t>(days + 1),
        static_cast<uint8_t>(minute_of_day / 60),
        static_cast<uint8_t>(minute_of_day % 60)};
    *out = result;
    return true;
}

extern "C" size_t cyberdeck_format_clock(char *buf, size_t cap,
                                          const cyberdeck_clock_time_t *clock)
{
    if (!valid_time(clock)) {
        return 0;
    }

    char formatted[sizeof("DD/MM/YYYY HH:MM")];
    const int length = snprintf(formatted, sizeof(formatted),
                                "%02u/%02u/%04d %02u:%02u",
                                static_cast<unsigned>(clock->day),
                                static_cast<unsigned>(clock->month),
                                static_cast<int>(clock->year),
                                static_cast<unsigned>(clock->hour),
                                static_cast<unsigned>(clock->minute));
    if (length < 0) {
        return 0;
    }
    if (buf != nullptr && cap != 0) {
        const size_t count = (cap - 1 < static_cast<size_t>(length))
                           ? cap - 1 : static_cast<size_t>(length);
        for (size_t i = 0; i < count; ++i) {
            buf[i] = formatted[i];
        }
        buf[count] = '\0';
    }
    return static_cast<size_t>(length);
}
