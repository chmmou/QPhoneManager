# A tool of the build: turns the Markdown handbook into the help files that
# the program embeds. It is not installed.
qt_add_executable(helpgen
    main.cpp
    HandbookChapter.h
    HandbookChapter.cpp
)
target_link_libraries(helpgen PRIVATE Qt6::Gui)
qpm_set_project_warnings(helpgen)
