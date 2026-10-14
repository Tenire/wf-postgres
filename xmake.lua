add_rules("mode.debug", "mode.release")

add_rules("plugin.compile_commands.autoupdate", {outputdir = "build"})

add_requires("workflow")
add_requires("openssl3")

target("wf_postgres")
    set_kind("static")
    set_languages("cxx11")
    add_files("src/factory/*.cc", "src/client/*.cc", "src/protocol/*.cc", "src/auth/*.cc")
    add_headerfiles("include/(*.h)")
    add_includedirs("include", "src/protocol", "src/auth")
    add_includedirs("src/factory", "src/client", "src/protocol", "src/auth", {public = false})
    add_packages("workflow", "openssl3")
    
    add_includedirs("include", {public = true})

option("tests", {description = "Build test programs", default = false, showmenu = true})
if has_config("tests") then
    includes("test")
end

option("tutorial", {description = "Build tutorial programs", default = true, showmenu = true})
if has_config("tutorial") then
    includes("tutorial")
end
