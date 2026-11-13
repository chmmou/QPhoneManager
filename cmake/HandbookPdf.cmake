find_program(QPM_PANDOC_EXECUTABLE NAMES pandoc)
find_program(QPM_WEASYPRINT_EXECUTABLE NAMES weasyprint)

# Adds the target "handbook_pdf", which typesets the handbook in German and
# English as doc/handbook/QPhoneManager-Handbuch.pdf and
# doc/handbook/QPhoneManager-Manual.pdf in the source tree. The target is not
# part of ALL and rebuilds both files on every run.
#
# TOPICS lists the chapters in the order of the book. Without pandoc or
# WeasyPrint the target still exists and fails with a message that names the
# missing program; configuring does not fail.
function(qpm_add_handbook_pdf_target)
    cmake_parse_arguments(PARSE_ARGV 0 arg "" "" "TOPICS")
    if(NOT arg_TOPICS)
        message(FATAL_ERROR "qpm_add_handbook_pdf_target: TOPICS is empty")
    endif()

    set(missing "")
    if(NOT QPM_PANDOC_EXECUTABLE)
        list(APPEND missing pandoc)
    endif()
    if(NOT QPM_WEASYPRINT_EXECUTABLE)
        list(APPEND missing weasyprint)
    endif()
    if(missing)
        list(JOIN missing " and " missing_names)
        message(STATUS "${missing_names} not found; the handbook_pdf target will fail")
        add_custom_target(handbook_pdf
            COMMAND "${CMAKE_COMMAND}" -E echo
                "handbook_pdf: ${missing_names} not found. Install them and configure again."
            COMMAND "${CMAKE_COMMAND}" -E false
            VERBATIM
        )
        return()
    endif()

    set(handbook_dir "${PROJECT_SOURCE_DIR}/doc/handbook")
    set(style_dir "${handbook_dir}/pdf")
    set(work_dir "${PROJECT_BINARY_DIR}/handbook_pdf")
    set(logo "${PROJECT_SOURCE_DIR}/res/icons/app/qphonemanager.svg")
    file(STRINGS "${PROJECT_SOURCE_DIR}/LICENSE" copyright REGEX "^Copyright " LIMIT_COUNT 1)
    if(NOT copyright)
        message(FATAL_ERROR "qpm_add_handbook_pdf_target: no copyright line in LICENSE")
    endif()
    list(JOIN arg_TOPICS "," topic_list)

    set(commands COMMAND "${CMAKE_COMMAND}" -E make_directory "${work_dir}")
    foreach(language IN ITEMS de en)
        if(language STREQUAL "de")
            set(book "Handbuch")
            set(contents "Inhalt")
        else()
            set(book "Manual")
            set(contents "Contents")
        endif()
        set(source "${handbook_dir}/${language}")
        set(chapters "${arg_TOPICS}")
        list(TRANSFORM chapters PREPEND "${source}/")
        list(TRANSFORM chapters APPEND ".md")
        set(html "${work_dir}/handbook_${language}.html")
        set(pdf "${handbook_dir}/QPhoneManager-${book}.pdf")
        list(APPEND commands
            COMMAND "${QPM_PANDOC_EXECUTABLE}"
                --from=gfm+implicit_figures-raw_html
                --to=html5
                --standalone
                "--template=${style_dir}/handbook.html"
                "--lua-filter=${style_dir}/chapters.lua"
                --toc
                --toc-depth=2
                "--metadata=topics:${topic_list}"
                "--metadata=lang:${language}"
                "--metadata=title:${PROJECT_NAME}"
                "--metadata=subtitle:${book}"
                "--metadata=version:${PROJECT_VERSION}"
                "--metadata=copyright:${copyright}"
                "--metadata=toc-title:${contents}"
                "--metadata=logo:${logo}"
                "--output=${html}"
                ${chapters}
            COMMAND "${QPM_WEASYPRINT_EXECUTABLE}"
                "--base-url=${source}/"
                "--stylesheet=${style_dir}/handbook.css"
                --pdf-tags
                --optimize-images
                "${html}"
                "${pdf}"
        )
    endforeach()

    # The PDFs are not declared as byproducts: they are versioned files in the
    # source tree, and a clean of the build would delete byproducts.
    add_custom_target(handbook_pdf
        ${commands}
        COMMENT "Typesetting the handbook as PDF"
        VERBATIM
    )
endfunction()
