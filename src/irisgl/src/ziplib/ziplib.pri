INCLUDEPATH += \
        $$PWD \
        $$PWD\extlibs \

DEPENDPATH += \
        $$PWD \
        $$PWD\extlibs \

HEADERS += \
        $$PWD/compression/bzip2/bzip2_decoder.h \
        $$PWD/compression/bzip2/bzip2_decoder_properties.h \
        $$PWD/compression/bzip2/bzip2_encoder.h \
        $$PWD/compression/bzip2/bzip2_encoder_properties.h \
        $$PWD/compression/compression_interface.h \
        $$PWD/compression/deflate/deflate_decoder.h \
        $$PWD/compression/deflate/deflate_decoder_properties.h \
        $$PWD/compression/deflate/deflate_encoder.h \
        $$PWD/compression/deflate/deflate_encoder_properties.h \
        $$PWD/compression/lzma/detail/lzma_alloc.h \
        $$PWD/compression/lzma/detail/lzma_handle.h \
        $$PWD/compression/lzma/detail/lzma_header.h \
        $$PWD/compression/lzma/detail/lzma_in_stream.h \
        $$PWD/compression/lzma/detail/lzma_out_stream.h \
        $$PWD/compression/lzma/lzma_decoder.h \
        $$PWD/compression/lzma/lzma_decoder_properties.h \
        $$PWD/compression/lzma/lzma_encoder.h \
        $$PWD/compression/lzma/lzma_encoder_properties.h \
        $$PWD/compression/store/store_decoder.h \
        $$PWD/compression/store/store_decoder_properties.h \
        $$PWD/compression/store/store_encoder.h \
        $$PWD/compression/store/store_encoder_properties.h \
        $$PWD/detail/EndOfCentralDirectoryBlock.h \
        $$PWD/detail/ZipCentralDirectoryFileHeader.h \
        $$PWD/detail/ZipGenericExtraField.h \
        $$PWD/detail/ZipLocalFileHeader.h \
        $$PWD/methods/Bzip2Method.h \
        $$PWD/methods/DeflateMethod.h \
        $$PWD/methods/ICompressionMethod.h \
        $$PWD/methods/LzmaMethod.h \
        $$PWD/methods/StoreMethod.h \
        $$PWD/methods/ZipMethodResolver.h \
        $$PWD/streams/compression_decoder_stream.h \
        $$PWD/streams/compression_encoder_stream.h \
        $$PWD/streams/crc32stream.h \
        $$PWD/streams/memstream.h \
        $$PWD/streams/nullstream.h \
        $$PWD/streams/serialization.h \
        $$PWD/streams/streambuffs/compression_decoder_streambuf.h \
        $$PWD/streams/streambuffs/compression_encoder_streambuf.h \
        $$PWD/streams/streambuffs/crc32_streambuf.h \
        $$PWD/streams/streambuffs/mem_streambuf.h \
        $$PWD/streams/streambuffs/null_streambuf.h \
        $$PWD/streams/streambuffs/sub_streambuf.h \
        $$PWD/streams/streambuffs/tee_streambuff.h \
        $$PWD/streams/streambuffs/zip_crypto_streambuf.h \
        $$PWD/streams/substream.h \
        $$PWD/streams/teestream.h \
        $$PWD/streams/zip_cryptostream.h \
        $$PWD/utils/enum_utils.h \
        $$PWD/utils/stream_utils.h \
        $$PWD/utils/time_utils.h \
        $$PWD/ZipArchive.h \
        $$PWD/ZipArchiveEntry.h \
        $$PWD/ZipFile.h \
        $$PWD/extlibs/lzma/7z.h \
        $$PWD/extlibs/lzma/7zAlloc.h \
        $$PWD/extlibs/lzma/7zBuf.h \
        $$PWD/extlibs/lzma/7zCrc.h \
        $$PWD/extlibs/lzma/7zFile.h \
        $$PWD/extlibs/lzma/7zVersion.h \
        $$PWD/extlibs/lzma/Alloc.h \
        $$PWD/extlibs/lzma/Bcj2.h \
        $$PWD/extlibs/lzma/Bra.h \
        $$PWD/extlibs/lzma/CpuArch.h \
        $$PWD/extlibs/lzma/Delta.h \
        $$PWD/extlibs/lzma/LzFind.h \
        $$PWD/extlibs/lzma/LzFindMt.h \
        $$PWD/extlibs/lzma/LzHash.h \
        $$PWD/extlibs/lzma/Lzma2Dec.h \
        $$PWD/extlibs/lzma/Lzma2Enc.h \
        $$PWD/extlibs/lzma/Lzma86.h \
        $$PWD/extlibs/lzma/LzmaDec.h \
        $$PWD/extlibs/lzma/LzmaEnc.h \
        $$PWD/extlibs/lzma/LzmaLib.h \
        $$PWD/extlibs/lzma/MtCoder.h \
        $$PWD/extlibs/lzma/Ppmd.h \
        $$PWD/extlibs/lzma/Ppmd7.h \
        $$PWD/extlibs/lzma/RotateDefs.h \
        $$PWD/extlibs/lzma/Sha256.h \
        $$PWD/extlibs/lzma/Threads.h \
        $$PWD/extlibs/lzma/Types.h \
        $$PWD/extlibs/lzma/Xz.h \
        $$PWD/extlibs/lzma/XzCrc64.h \
        $$PWD/extlibs/lzma/XzEnc.h \
        $$PWD/extlibs/bzip2/bzlib.h \
        $$PWD/extlibs/bzip2/bzlib_private.h

SOURCES += \
        $$PWD/detail/EndOfCentralDirectoryBlock.cpp \
        $$PWD/detail/ZipCentralDirectoryFileHeader.cpp \
        $$PWD/detail/ZipGenericExtraField.cpp \
        $$PWD/detail/ZipLocalFileHeader.cpp \
        $$PWD/ZipArchive.cpp \
        $$PWD/ZipArchiveEntry.cpp \
        $$PWD/ZipFile.cpp \
        $$PWD/extlibs/lzma/7zAlloc.c \
        $$PWD/extlibs/lzma/7zBuf.c \
        $$PWD/extlibs/lzma/7zBuf2.c \
        $$PWD/extlibs/lzma/7zCrc.c \
        $$PWD/extlibs/lzma/7zCrcOpt.c \
        $$PWD/extlibs/lzma/7zDec.c \
        $$PWD/extlibs/lzma/7zFile.c \
        $$PWD/extlibs/lzma/7zIn.c \
        $$PWD/extlibs/lzma/7zStream.c \
        $$PWD/extlibs/lzma/Alloc.c \
        $$PWD/extlibs/lzma/Bcj2.c \
        $$PWD/extlibs/lzma/Bra.c \
        $$PWD/extlibs/lzma/Bra86.c \
        $$PWD/extlibs/lzma/BraIA64.c \
        $$PWD/extlibs/lzma/CpuArch.c \
        $$PWD/extlibs/lzma/Delta.c \
        $$PWD/extlibs/lzma/LzFind.c \
        $$PWD/extlibs/lzma/LzFindMt.c \
        $$PWD/extlibs/lzma/Lzma2Dec.c \
        $$PWD/extlibs/lzma/Lzma2Enc.c \
        $$PWD/extlibs/lzma/Lzma86Dec.c \
        $$PWD/extlibs/lzma/Lzma86Enc.c \
        $$PWD/extlibs/lzma/LzmaDec.c \
        $$PWD/extlibs/lzma/LzmaEnc.c \
        $$PWD/extlibs/lzma/LzmaLib.c \
        $$PWD/extlibs/lzma/MtCoder.c \
        $$PWD/extlibs/lzma/Ppmd7.c \
        $$PWD/extlibs/lzma/Ppmd7Dec.c \
        $$PWD/extlibs/lzma/Ppmd7Enc.c \
        $$PWD/extlibs/lzma/Sha256.c \
        $$PWD/extlibs/lzma/Threads.c \
        $$PWD/extlibs/lzma/Xz.c \
        $$PWD/extlibs/lzma/XzCrc64.c \
        $$PWD/extlibs/lzma/XzDec.c \
        $$PWD/extlibs/lzma/XzEnc.c \
        $$PWD/extlibs/lzma/XzIn.c \
        $$PWD/extlibs/bzip2/blocksort.c \
        $$PWD/extlibs/bzip2/bzerror.c \
        $$PWD/extlibs/bzip2/bzlib.c \
        $$PWD/extlibs/bzip2/compress.c \
        $$PWD/extlibs/bzip2/crctable.c \
        $$PWD/extlibs/bzip2/decompress.c \
        $$PWD/extlibs/bzip2/huffman.c \
        $$PWD/extlibs/bzip2/randtable.c

CONFIG += c++11