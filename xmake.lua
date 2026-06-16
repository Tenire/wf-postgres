add_rules("mode.debug", "mode.release")

-- 自动生成 compile_commands.json 并放到 build 目录
add_rules("plugin.compile_commands.autoupdate", {outputdir = "build"})

-- 使用官方 xrepo 包，而不是引入本地源码
add_requires("workflow")
add_requires("openssl") -- SCRAM-SHA-256 crypto dependency

-- 定义我们的 wf-postgres 静态库
target("wf_postgres")
    set_kind("static")
    set_languages("cxx11") -- 保持和 workflow 同步，避免倒逼用户升级 C++ 标准
    add_files("src/client/*.cc", "src/protocol/PostgresMessage.cc", "src/protocol/SSLWrapper.cc", "src/auth/*.cc")
    add_headerfiles("include/(*.h)")
    add_includedirs("include", "src/client", "src/protocol", "src/auth")
    add_packages("workflow", "openssl")
    
    -- 添加头文件搜索路径
    add_includedirs("include", {public = true})

option("tests", {description = "Build test programs", default = false, showmenu = true})
if has_config("tests") then
    includes("test")
end

option("tutorial", {description = "Build tutorial programs", default = true, showmenu = true})
if has_config("tutorial") then
    includes("tutorial")
end
