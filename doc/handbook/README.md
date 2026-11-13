# Handbook

The handbook of QPhoneManager in German (`de/`) and English (`en/`). The
build turns each language into a help file that the program shows with F1.
The tool for that is `tools/helpgen`.

## One chapter per area

Each area of the program has one chapter, in the same file name in both
languages. The topic is the id that F1 looks for, and the top-level heading
of the chapter gets that id as its anchor.

| Area | File | Topic and anchor |
|---|---|---|
| Getting started (the start of the help) | `getting-started.md` | `getting-started` |
| Overview | `overview.md` | `overview` |
| Contacts | `contacts.md` | `contacts` |
| Calls | `calls.md` | `calls` |
| Messages | `messages.md` | `messages` |
| Calendar | `calendar.md` | `calendar` |
| Tasks | `tasks.md` | `tasks` |
| Notes | `notes.md` | `notes` |
| Files | `files.md` | `files` |
| Apps | `apps.md` | `apps` |
| Screen | `screen.md` | `screen` |
| Notifications | `notifications.md` | `notifications` |
| Back up and restore | `backup.md` | `backup` |
| Settings | `settings.md` | `settings` |

The table of contents of the help follows this order. The list itself lives
in `src/CMakeLists.txt` as `qpm_help_topics`; a new area of the program
needs an entry there and a chapter in both languages.

## Rules the build checks

The build stops with a message when one of these is broken:

- A chapter has exactly one top-level heading (`# Title`). Its text is the
  title in the table of contents. Use `##` and below inside the chapter.
- Every topic above has a file, and there is no other `.md` file in `de/`
  or `en/`.
- A link to another chapter names its file: `[Contacts](contacts.md)`. A link
  out of the program starts with `https://`. Nothing else is allowed.
- An image lies in `images/` next to the chapter, as PNG or JPEG, and the
  chapter links it relatively: `![Contact list](images/contacts-list.png)`.
  The file name uses letters, digits, `.`, `_` and `-`.

HTML inside the Markdown shows as plain text. Tables, lists, bold, italic and
code are fine.

## Rules for the text

- German is the main language; English says the same.
- German addresses the reader as "Sie", like the program.
- `getting-started.md` links to every other chapter. A test of the program
  relies on these links to check that links work by keyboard.
- Screenshots come from the program in the language of the chapter.
