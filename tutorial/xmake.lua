target("tutorial-01-postgres-cli")
    set_kind("binary")
    set_languages("cxx11")
    add_deps("wf_postgres")
    add_packages("workflow", "openssl")
    add_files("tutorial-01-postgres-cli.cc")

target("tutorial-02-postgres-transaction")
    set_kind("binary")
    set_languages("cxx11")
    add_deps("wf_postgres")
    add_packages("workflow", "openssl")
    add_files("tutorial-02-postgres-transaction.cc")




