target("test_main")
    set_kind("binary")
    set_languages("cxx11")
    add_deps("wf_postgres")
    add_packages("workflow", "openssl")
    add_files("test_main.cc")

target("test_prepare")
    set_kind("binary")
    set_languages("cxx11")
    add_deps("wf_postgres")
    add_packages("workflow", "openssl")
    add_files("test_prepare.cc")
    if is_plat("linux") and is_mode("asan") then
        add_runenvs("LSAN_OPTIONS", "suppressions=" .. os.projectdir() .. "/test/asan.supp")
    end

target("test_integration")
    set_kind("binary")
    set_languages("cxx11")
    add_deps("wf_postgres")
    add_packages("workflow", "openssl")
    add_files("test_integration.cc")
    if is_plat("linux") and is_mode("asan") then
        add_runenvs("LSAN_OPTIONS", "suppressions=" .. os.projectdir() .. "/test/asan.supp")
    end

target("test_fuzzer")
    set_kind("binary")
    set_languages("cxx11")
    add_deps("wf_postgres")
    add_packages("workflow", "openssl")
    add_files("test_fuzzer.cc")
    if is_plat("linux") and is_mode("asan") then
        add_runenvs("LSAN_OPTIONS", "suppressions=" .. os.projectdir() .. "/test/asan.supp")
    end

target("test_cancel")
    set_kind("binary")
    set_languages("cxx11")
    add_deps("wf_postgres")
    add_packages("workflow", "openssl")
    add_files("test_cancel.cc")
    if is_plat("linux") and is_mode("asan") then
        add_runenvs("LSAN_OPTIONS", "suppressions=" .. os.projectdir() .. "/test/asan.supp")
    end

target("test_ssl")
    set_kind("binary")
    set_languages("cxx11")
    add_deps("wf_postgres")
    add_packages("workflow", "openssl")
    add_files("test_ssl.cc")
    if is_plat("linux") and is_mode("asan") then
        add_runenvs("LSAN_OPTIONS", "suppressions=" .. os.projectdir() .. "/test/asan.supp")
    end

target("test_notify")
    set_kind("binary")
    set_languages("cxx11")
    add_deps("wf_postgres")
    add_packages("workflow", "openssl")
    add_files("test_notify.cc")
    if is_plat("linux") and is_mode("asan") then
        add_runenvs("LSAN_OPTIONS", "suppressions=" .. os.projectdir() .. "/test/asan.supp")
    end

target("test_copy")
    set_kind("binary")
    set_languages("cxx11")
    add_deps("wf_postgres")
    add_packages("workflow", "openssl")
    add_files("test_copy.cc")
    if is_plat("linux") and is_mode("asan") then
        add_runenvs("LSAN_OPTIONS", "suppressions=" .. os.projectdir() .. "/test/asan.supp")
    end

target("test_reconnect")
    set_kind("binary")
    set_languages("cxx11")
    add_deps("wf_postgres")
    add_packages("workflow", "openssl")
    add_files("test_reconnect.cc")
    if is_plat("linux") and is_mode("asan") then
        add_runenvs("LSAN_OPTIONS", "suppressions=" .. os.projectdir() .. "/test/asan.supp")
    end

target("test_protocol_negotiation")
    set_kind("binary")
    set_languages("cxx11")
    add_deps("wf_postgres")
    add_packages("workflow", "openssl")
    add_files("test_protocol_negotiation.cc")
    if is_plat("linux") and is_mode("asan") then
        add_runenvs("LSAN_OPTIONS", "suppressions=" .. os.projectdir() .. "/test/asan.supp")
    end

target("test_state")
    set_kind("binary")
    set_languages("cxx11")
    add_deps("wf_postgres")
    add_packages("workflow", "openssl")
    add_files("test_state.cc")
    if is_plat("linux") and is_mode("asan") then
        add_runenvs("LSAN_OPTIONS", "suppressions=" .. os.projectdir() .. "/test/asan.supp")
    end

target("test_error_codes")
    set_kind("binary")
    set_languages("cxx11")
    add_deps("wf_postgres")
    add_packages("workflow", "openssl")
    add_files("test_error_codes.cc")
    if is_plat("linux") and is_mode("asan") then
        add_runenvs("LSAN_OPTIONS", "suppressions=" .. os.projectdir() .. "/test/asan.supp")
    end

target("test_startup_params")
    set_kind("binary")
    set_languages("cxx11")
    add_deps("wf_postgres")
    add_packages("workflow", "openssl")
    add_files("test_startup_params.cc")
    if is_plat("linux") and is_mode("asan") then
        add_runenvs("LSAN_OPTIONS", "suppressions=" .. os.projectdir() .. "/test/asan.supp")
    end

target("test_command_tag")
    set_kind("binary")
    set_languages("cxx11")
    add_deps("wf_postgres")
    add_packages("workflow", "openssl")
    add_files("test_command_tag.cc")
    if is_plat("linux") and is_mode("asan") then
        add_runenvs("LSAN_OPTIONS", "suppressions=" .. os.projectdir() .. "/test/asan.supp")
    end

target("test_types")
    set_kind("binary")
    set_languages("cxx11")
    add_deps("wf_postgres")
    add_packages("workflow", "openssl")
    add_files("test_types.cc")
    if is_plat("linux") and is_mode("asan") then
        add_runenvs("LSAN_OPTIONS", "suppressions=" .. os.projectdir() .. "/test/asan.supp")
    end

target("test_disconnect_factory")
    set_kind("binary")
    set_languages("cxx11")
    add_deps("wf_postgres")
    add_packages("workflow", "openssl")
    add_files("test_disconnect_factory.cc")
    if is_plat("linux") and is_mode("asan") then
        add_runenvs("LSAN_OPTIONS", "suppressions=" .. os.projectdir() .. "/test/asan.supp")
    end

target("test_notice")
    set_kind("binary")
    set_languages("cxx11")
    add_deps("wf_postgres")
    add_packages("workflow", "openssl")
    add_files("test_notice.cc")
    if is_plat("linux") and is_mode("asan") then
        add_runenvs("LSAN_OPTIONS", "suppressions=" .. os.projectdir() .. "/test/asan.supp")
    end
