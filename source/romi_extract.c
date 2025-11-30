#include "romi_extract.h"
#include "romi.h"
#include "romi_utils.h"

#include <stdlib.h>
#include <string.h>
#include <zlib.h>

#define ZIP_LOCAL_HEADER_SIG    0x04034b50
#define ZIP_CENTRAL_DIR_SIG     0x02014b50
#define ZIP_END_CENTRAL_DIR_SIG 0x06054b50

#define ZIP_METHOD_STORED   0
#define ZIP_METHOD_DEFLATE  8

#define EXTRACT_BUFFER_SIZE (256 * 1024)

typedef struct {
    uint32_t signature;
    uint16_t version_needed;
    uint16_t flags;
    uint16_t compression;
    uint16_t mod_time;
    uint16_t mod_date;
    uint32_t crc32;
    uint32_t compressed_size;
    uint32_t uncompressed_size;
    uint16_t filename_len;
    uint16_t extra_len;
} __attribute__((packed)) ZipLocalHeader;

static int cancelled = 0;

static int create_parent_dirs(const char* filepath)
{
    char path[256];
    romi_strncpy(path, sizeof(path), filepath);

    for (char* p = path + 1; *p; p++)
    {
        if (*p == '/')
        {
            *p = 0;
            romi_mkdirs(path);
            *p = '/';
        }
    }
    return 1;
}

static RomiExtractResult extract_stored(void* zf, void* outf, uint32_t size, uint8_t* buffer)
{
    uint32_t remaining = size;

    while (remaining > 0)
    {
        if (cancelled) return ExtractCancelled;

        uint32_t chunk = remaining > EXTRACT_BUFFER_SIZE ? EXTRACT_BUFFER_SIZE : remaining;

        if (!romi_read(zf, buffer, chunk))
            return ExtractErrorRead;

        if (!romi_write(outf, buffer, chunk))
            return ExtractErrorWrite;

        remaining -= chunk;
    }

    return ExtractOK;
}

static RomiExtractResult extract_deflate(void* zf, void* outf, uint32_t comp_size, uint32_t uncomp_size, uint8_t* in_buf, uint8_t* out_buf)
{
    z_stream strm;
    memset(&strm, 0, sizeof(strm));

    if (inflateInit2(&strm, -MAX_WBITS) != Z_OK)
        return ExtractErrorDecompress;

    uint32_t remaining_in = comp_size;
    RomiExtractResult result = ExtractOK;

    while (remaining_in > 0 || strm.avail_in > 0)
    {
        if (cancelled)
        {
            result = ExtractCancelled;
            break;
        }

        if (strm.avail_in == 0 && remaining_in > 0)
        {
            uint32_t chunk = remaining_in > EXTRACT_BUFFER_SIZE ? EXTRACT_BUFFER_SIZE : remaining_in;

            if (!romi_read(zf, in_buf, chunk))
            {
                result = ExtractErrorRead;
                break;
            }

            strm.avail_in = chunk;
            strm.next_in = in_buf;
            remaining_in -= chunk;
        }

        strm.avail_out = EXTRACT_BUFFER_SIZE;
        strm.next_out = out_buf;

        int ret = inflate(&strm, Z_NO_FLUSH);

        if (ret == Z_STREAM_ERROR || ret == Z_DATA_ERROR || ret == Z_MEM_ERROR)
        {
            result = ExtractErrorDecompress;
            break;
        }

        uint32_t have = EXTRACT_BUFFER_SIZE - strm.avail_out;
        if (have > 0)
        {
            if (!romi_write(outf, out_buf, have))
            {
                result = ExtractErrorWrite;
                break;
            }
        }

        if (ret == Z_STREAM_END)
            break;
    }

    inflateEnd(&strm);
    return result;
}

RomiExtractResult romi_extract_zip(const char* zip_path, const char* dest_folder, RomiExtractProgress progress)
{
    void* zf = romi_open(zip_path);
    if (!zf)
        return ExtractErrorOpen;

    uint8_t* in_buffer = malloc(EXTRACT_BUFFER_SIZE);
    uint8_t* out_buffer = malloc(EXTRACT_BUFFER_SIZE);

    if (!in_buffer || !out_buffer)
    {
        if (in_buffer) free(in_buffer);
        if (out_buffer) free(out_buffer);
        romi_close(zf);
        return ExtractErrorMemory;
    }

    cancelled = 0;

    int64_t total_size = romi_get_size(zip_path);
    uint64_t extracted = 0;
    RomiExtractResult result = ExtractOK;
    ZipLocalHeader header;

    while (romi_read(zf, &header, sizeof(header)))
    {
        if (cancelled)
        {
            result = ExtractCancelled;
            break;
        }

        uint32_t sig = get32le((uint8_t*)&header.signature);

        if (sig == ZIP_CENTRAL_DIR_SIG || sig == ZIP_END_CENTRAL_DIR_SIG)
            break;

        if (sig != ZIP_LOCAL_HEADER_SIG)
        {
            result = ExtractErrorFormat;
            break;
        }

        uint16_t compression = get16le((uint8_t*)&header.compression);
        uint32_t comp_size = get32le((uint8_t*)&header.compressed_size);
        uint32_t uncomp_size = get32le((uint8_t*)&header.uncompressed_size);
        uint16_t filename_len = get16le((uint8_t*)&header.filename_len);
        uint16_t extra_len = get16le((uint8_t*)&header.extra_len);

        char filename[256];
        if (filename_len >= sizeof(filename))
            filename_len = sizeof(filename) - 1;

        if (!romi_read(zf, filename, filename_len))
        {
            result = ExtractErrorRead;
            break;
        }
        filename[filename_len] = 0;

        if (extra_len > 0)
        {
            uint8_t skip[256];
            uint16_t remaining = extra_len;
            while (remaining > 0)
            {
                uint16_t chunk = remaining > sizeof(skip) ? sizeof(skip) : remaining;
                if (!romi_read(zf, skip, chunk))
                {
                    result = ExtractErrorRead;
                    break;
                }
                remaining -= chunk;
            }
            if (result != ExtractOK)
                break;
        }

        int is_directory = (filename_len > 0 && filename[filename_len - 1] == '/');

        char dest_path[512];
        romi_snprintf(dest_path, sizeof(dest_path), "%s/%s", dest_folder, filename);

        if (progress)
            progress(filename, extracted, total_size);

        if (is_directory)
        {
            romi_mkdirs(dest_path);
        }
        else if (comp_size > 0)
        {
            create_parent_dirs(dest_path);

            void* outf = romi_create(dest_path);
            if (!outf)
            {
                result = ExtractErrorWrite;
                break;
            }

            if (compression == ZIP_METHOD_STORED)
            {
                result = extract_stored(zf, outf, comp_size, in_buffer);
            }
            else if (compression == ZIP_METHOD_DEFLATE)
            {
                result = extract_deflate(zf, outf, comp_size, uncomp_size, in_buffer, out_buffer);
            }
            else
            {
                LOG("unsupported compression method %d for %s", compression, filename);

                uint32_t remaining = comp_size;
                while (remaining > 0)
                {
                    uint32_t chunk = remaining > EXTRACT_BUFFER_SIZE ? EXTRACT_BUFFER_SIZE : remaining;
                    if (!romi_read(zf, in_buffer, chunk))
                    {
                        result = ExtractErrorRead;
                        break;
                    }
                    remaining -= chunk;
                }
            }

            romi_close(outf);

            if (result != ExtractOK)
                break;

            extracted += uncomp_size;
        }
    }

    if (progress && result == ExtractOK)
        progress(NULL, extracted, extracted);

    free(in_buffer);
    free(out_buffer);
    romi_close(zf);

    return result;
}

const char* romi_extract_error_string(RomiExtractResult result)
{
    switch (result)
    {
        case ExtractOK:               return "OK";
        case ExtractErrorOpen:        return "Cannot open ZIP file";
        case ExtractErrorRead:        return "Read error";
        case ExtractErrorFormat:      return "Invalid ZIP format";
        case ExtractErrorMemory:      return "Out of memory";
        case ExtractErrorWrite:       return "Write error";
        case ExtractErrorDecompress:  return "Decompression error";
        case ExtractCancelled:        return "Cancelled";
        default:                      return "Unknown error";
    }
}

int romi_is_zip_file(const char* path)
{
    if (!path) return 0;

    const char* ext = romi_strrchr(path, '.');
    if (!ext) return 0;

    return (romi_stricmp(ext, ".zip") == 0);
}

void romi_extract_cancel(void)
{
    cancelled = 1;
}
