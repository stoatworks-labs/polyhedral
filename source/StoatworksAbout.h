/*
 * Stoatworks Labs - About window data for Polyhedral.
 *
 * PROVISIONAL HAND COPY, adapted from boreal's on 2026-10-04 (as graticule's
 * was): polyhedral is not registered in the website's projects.json yet, so
 * stoatworks-backend/scripts/sync-about.py has nothing to generate this from.
 * `guide` is empty because no user guide exists, which leaves three buttons.
 * The first sync after registration overwrites this file.
 */
#pragma once

namespace stoatworks::about
{
    inline constexpr auto name = "Polyhedral";
    inline constexpr auto slug = "polyhedral";
    inline constexpr auto hook = "Polyhedral dice thrown onto the table, for Resolume";
    inline constexpr auto licence = "MIT";
    inline constexpr auto guide = "";
    inline constexpr auto page = "https://stoatworks-labs.com/software/polyhedral/";
    inline constexpr auto repo = "https://github.com/stoatworks-labs/polyhedral";
    inline constexpr auto versionFallback = "v0.1.0";

    inline constexpr auto org = "Stoatworks Labs";
    inline constexpr auto home = "https://stoatworks-labs.com";
    inline constexpr auto tagline = "Open tools for the people who run the show.";

    /* The canonical funding links, matching FUNDING.yml and the support footer. */
    struct Link { const char* name; const char* url; };
    inline constexpr Link funding[] = {
        { "GitHub Sponsors", "https://github.com/sponsors/stoatworks-labs" },
        { "Ko-fi", "https://ko-fi.com/stoatworkslabs" },
        { "Patreon", "https://patreon.com/StoatworksLabs" },
        { "Liberapay", "https://liberapay.com/stoatworks-labs" },
    };
}
