set(BREAKPAD_SRCS ${BREAKPAD_SRCS}
    breakpad/src/common/string_conversion.cc
    breakpad/src/common/convert_UTF.c
    breakpad/src/common/md5.cc
    breakpad/src/common/linux/guid_creator.cc 
    breakpad/src/common/linux/file_id.cc 
    breakpad/src/common/linux/elfutils.cc 
    breakpad/src/common/linux/memory_mapped_file.cc 
    breakpad/src/common/linux/safe_readlink.cc 
    breakpad/src/common/linux/linux_libc_support.cc
    breakpad/src/common/linux/elf_core_dump.cc
    breakpad/src/client/minidump_file_writer.cc
    breakpad/src/client/linux/log/log.cc 
    breakpad/src/client/linux/crash_generation/crash_generation_client.cc 
    breakpad/src/client/linux/dump_writer_common/thread_info.cc 
    breakpad/src/client/linux/dump_writer_common/ucontext_reader.cc 
    breakpad/src/client/linux/microdump_writer/microdump_writer.cc 
    breakpad/src/client/linux/minidump_writer/linux_dumper.cc 
    breakpad/src/client/linux/minidump_writer/linux_core_dumper.cc 
    breakpad/src/client/linux/minidump_writer/linux_ptrace_dumper.cc 
    breakpad/src/client/linux/minidump_writer/minidump_writer.cc 
    breakpad/src/client/linux/handler/minidump_descriptor.cc 
    breakpad/src/client/linux/handler/exception_handler.cc 
)

find_package (Threads REQUIRED)
set(BREAKPAD_LIBS ${BREAKPAD_LIBS} ${CMAKE_THREAD_LIBS_INIT})
