using System;
using System.Collections.Generic;

namespace TTSL.Services;

public static class KrangleService
{
    private static readonly string[] ExerciseWords =
    {
        "Pushup", "Squat", "Lunge", "Plank", "Burpee", "Crunch", "Deadlift",
        "Curl", "Press", "Pullup", "Shrug", "Thrust", "Bridge", "Flutter",
        "Situp", "Sprawl", "Kata", "Kihon", "Kumite", "Ukemi", "Breakfall",
        "Sweep", "Roundhouse", "Jab", "Hook", "Cross", "Uppercut", "Parry",
        "Block", "Guard", "Stance", "Strike", "Punch", "Kick", "Elbow",
        "Knee", "Clinch", "Throw", "Grapple", "Armbar", "Choke", "Dodge",
        "Weave", "Slip", "Roll", "Feint", "Riposte", "Sprint", "Bench",
        "Clean", "Snatch", "Jerk", "Row", "Dip", "Step", "Jump", "Dash",
        "March", "Drill", "Crawl", "Climb", "Planche", "Muscle", "Lever",
        "Pistol", "Dragon", "Crane", "Tiger", "Mantis", "Viper", "Eagle",
    };

    private static readonly Dictionary<string, string> Cache = new();

    public static void ClearCache() => Cache.Clear();

    public static string KrangleName(string originalName)
    {
        if (string.IsNullOrWhiteSpace(originalName))
            return originalName;

        if (Cache.TryGetValue(originalName, out var cached))
            return cached;

        var atIdx = originalName.IndexOf('@');
        var charPart = atIdx >= 0 ? originalName[..atIdx] : originalName;
        var serverPart = atIdx >= 0 ? originalName[(atIdx + 1)..] : string.Empty;

        var hash = GetStableHash(charPart);
        var rng = new Random(hash);

        var nameParts = charPart.Split(' ', StringSplitOptions.RemoveEmptyEntries);
        var first = ExerciseWords[rng.Next(ExerciseWords.Length)];
        var last = nameParts.Length > 1 ? ExerciseWords[rng.Next(ExerciseWords.Length)] : string.Empty;

        if (first.Length > 14)
            first = first[..14];

        if (last.Length > 14)
            last = last[..14];

        if (last.Length > 0 && first.Length + 1 + last.Length > 22)
            last = last[..Math.Max(1, 22 - first.Length - 1)];

        var result = last.Length > 0 ? $"{first} {last}" : first;

        if (!string.IsNullOrEmpty(serverPart))
            result = $"{result}@{KrangleServer(serverPart)}";

        Cache[originalName] = result;
        return result;
    }

    public static string KrangleServer(string serverName)
    {
        if (string.IsNullOrWhiteSpace(serverName))
            return serverName;

        var key = $"srv:{serverName}";
        if (Cache.TryGetValue(key, out var cached))
            return cached;

        var hash = GetStableHash(serverName);
        var rng = new Random(hash);
        var word = ExerciseWords[rng.Next(ExerciseWords.Length)];
        if (word.Length > 25)
            word = word[..25];

        Cache[key] = word;
        return word;
    }

    private static int GetStableHash(string input)
    {
        unchecked
        {
            var hash = 17;
            foreach (var c in input)
                hash = (hash * 31) + c;

            return hash;
        }
    }
}
