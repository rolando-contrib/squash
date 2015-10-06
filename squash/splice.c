/* Copyright (c) 2015 The Squash Authors
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use, copy,
 * modify, merge, publish, distribute, sublicense, and/or sell copies
 * of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * Authors:
 *   Evan Nemerson <evan@nemerson.com>
 */

#define _FILE_OFFSET_BITS 64
#define _POSIX_C_SOURCE 200112L

#define _DEFAULT_SOURCE
#define _BSD_SOURCE

#include "internal.h"

#include <string.h>

/**
 * @defgroup Splicing
 * @brief Splicing functions
 *
 * These functions implement a convenient API for copying directly
 * from one file (or file-like stream) to another.
 *
 * @{
 */

/**
 * @brief compress or decompress the contents of one file to another
 *
 * This function will attempt to compress or decompress the contents
 * of one file to another.  It will attempt to use memory-mapped files
 * in order to reduce memory usage and increase performance, and so
 * should be preferred over writing similar code manually.
 *
 * @param fp_in the input *FILE* pointer
 * @param fp_out the output *FILE* pointer
 * @param length number of bytes (uncompressed) to transfer from @a
 *   fp_in to @a fp_out, or 0 to transfer the entire file
 * @param stream_type whether to compress or decompress the data
 * @param codec the name of the codec to use
 * @param ... list of options (with a *NULL* sentinel)
 * @returns @ref SQUASH_OK on success, or a negative error code on
 *   failure
 */
SquashStatus
squash_splice (const char* codec, SquashStreamType stream_type, FILE* fp_out, FILE* fp_in, size_t length, ...) {
  assert (fp_in != NULL);
  assert (fp_out != NULL);
  assert (stream_type == SQUASH_STREAM_COMPRESS || stream_type == SQUASH_STREAM_DECOMPRESS);

  SquashCodec* codec_i = squash_get_codec (codec);
  if (codec_i == NULL)
    return squash_error (SQUASH_BAD_PARAM);

  SquashOptions* options = NULL;
  va_list ap;
  va_start (ap, length);
  options = squash_options_newv (codec_i, ap);
  va_end (ap);

  return squash_splice_codec_with_options (codec_i, stream_type, fp_out, fp_in, length, options);
}

/**
 * @brief compress or decompress the contents of one file to another
 *
 * This function will attempt to compress or decompress the contents
 * of one file to another.  It will attempt to use memory-mapped files
 * in order to reduce memory usage and increase performance, and so
 * should be preferred over writing similar code manually.
 *
 * @param fp_in the input *FILE* pointer
 * @param fp_out the output *FILE* pointer
 * @param length number of bytes (uncompressed) to transfer from @a
 *   fp_in to @a fp_out
 * @param stream_type whether to compress or decompress the data
 * @param codec codec to use
 * @param ... list of options (with a *NULL* sentinel)
 * @returns @ref SQUASH_OK on success, or a negative error code on
 *   failure
 */
SquashStatus
squash_splice_codec (SquashCodec* codec, SquashStreamType stream_type, FILE* fp_out, FILE* fp_in, size_t length, ...) {
  assert (fp_in != NULL);
  assert (fp_out != NULL);
  assert (stream_type == SQUASH_STREAM_COMPRESS || stream_type == SQUASH_STREAM_DECOMPRESS);
  assert (codec != NULL);

  SquashOptions* options = NULL;
  va_list ap;
  va_start (ap, length);
  options = squash_options_newv (codec, ap);
  va_end (ap);

  return squash_splice_codec_with_options (codec, stream_type, fp_out, fp_in, length, options);
}

/**
 * @brief compress or decompress the contents of one file to another
 *
 * This function will attempt to compress or decompress the contents
 * of one file to another.  It will attempt to use memory-mapped files
 * in order to reduce memory usage and increase performance, and so
 * should be preferred over writing similar code manually.
 *
 * @param fp_in the input *FILE* pointer
 * @param fp_out the output *FILE* pointer
 * @param length number of bytes (uncompressed) to transfer from @a
 *   fp_in to @a fp_out
 * @param stream_type whether to compress or decompress the data
 * @param codec name of the codec to use
 * @param options options to pass to the codec
 * @returns @ref SQUASH_OK on success, or a negative error code on
 *   failure
 */
SquashStatus
squash_splice_with_options (const char* codec, SquashStreamType stream_type, FILE* fp_out, FILE* fp_in, size_t length, SquashOptions* options) {
  assert (fp_in != NULL);
  assert (fp_out != NULL);
  assert (stream_type == SQUASH_STREAM_COMPRESS || stream_type == SQUASH_STREAM_DECOMPRESS);

  SquashCodec* codec_i = squash_get_codec (codec);
  if (codec_i == NULL)
    return squash_error (SQUASH_BAD_PARAM);

  return squash_splice_codec_with_options (codec_i, stream_type, fp_out, fp_in, length, options);
}

static SquashStatus
squash_splice_map (FILE* fp_in, FILE* fp_out, size_t length, SquashStreamType stream_type, SquashCodec* codec, SquashOptions* options) {
  SquashStatus res = SQUASH_FAILED;
  SquashMappedFile mapped_in = squash_mapped_file_empty;
  SquashMappedFile mapped_out = squash_mapped_file_empty;

  if (stream_type == SQUASH_STREAM_COMPRESS) {
    if (!squash_mapped_file_init (&mapped_in, fp_in, length, false))
      goto cleanup;

    const size_t max_output_length = squash_codec_get_max_compressed_size(codec, mapped_in.length);
    if (!squash_mapped_file_init (&mapped_out, fp_out, max_output_length, true))
      goto cleanup;

    res = squash_codec_compress_with_options (codec, &mapped_out.length, mapped_out.data, mapped_in.length, mapped_in.data, options);
    if (res != SQUASH_OK)
      goto cleanup;

    squash_mapped_file_destroy (&mapped_in, true);
    squash_mapped_file_destroy (&mapped_out, true);
  } else {
    if (!squash_mapped_file_init (&mapped_in, fp_in, 0, false))
      goto cleanup;

    const SquashCodecInfo codec_info = squash_codec_get_info (codec);
    const bool knows_uncompressed = ((codec_info & SQUASH_CODEC_INFO_KNOWS_UNCOMPRESSED_SIZE) == SQUASH_CODEC_INFO_KNOWS_UNCOMPRESSED_SIZE);

    size_t max_output_length = knows_uncompressed ?
      squash_codec_get_uncompressed_size(codec, mapped_in.length, mapped_in.data) :
      squash_npot (mapped_in.length) << 3;

    do {
      if (!squash_mapped_file_init (&mapped_out, fp_out, max_output_length, true))
        goto cleanup;

      res = squash_codec_decompress_with_options (codec, &mapped_out.length, mapped_out.data, mapped_in.length, mapped_in.data, options);
      if (res == SQUASH_OK) {
        squash_mapped_file_destroy (&mapped_in, true);
        squash_mapped_file_destroy (&mapped_out, true);
      } else {
        max_output_length <<= 1;
      }
    } while (!knows_uncompressed && res == SQUASH_BUFFER_FULL);
  }

 cleanup:

  squash_mapped_file_destroy (&mapped_in, false);
  squash_mapped_file_destroy (&mapped_out, false);

  return res;
}

static SquashStatus
squash_splice_stream (FILE* fp_in,
                      FILE* fp_out,
                      size_t length,
                      SquashStreamType stream_type,
                      SquashCodec* codec,
                      SquashOptions* options) {
  SquashStatus res = SQUASH_FAILED;
  SquashFile* file = NULL;
  size_t remaining = length;
  uint8_t* data = NULL;
  size_t data_length = 0;
#if defined(SQUASH_MMAP_IO)
  bool first_block = true;
  SquashMappedFile map = squash_mapped_file_empty;

  if (stream_type == SQUASH_STREAM_COMPRESS) {
    file = squash_file_steal_codec_with_options (fp_out, codec, options);
    assert (file != NULL);

    while (length == 0 || remaining != 0) {
      const size_t req_size = (length == 0 || remaining > SQUASH_FILE_BUF_SIZE) ? SQUASH_FILE_BUF_SIZE : remaining;
      if (!squash_mapped_file_init_full(&map, fp_in, req_size, true, false)) {
        if (first_block)
          goto nomap;
        else {
          break;
        }
      } else {
        first_block = false;
      }

      res = squash_file_write (file, map.length, map.data);
      if (res != SQUASH_OK)
        goto cleanup;

      if (length != 0)
        remaining -= map.length;
      squash_mapped_file_destroy (&map, true);
    }
  } else { /* stream_type == SQUASH_STREAM_DECOMPRESS */
    file = squash_file_steal_codec_with_options (fp_in, codec, options);
    assert (file != NULL);

    while (length == 0 || remaining > 0) {
      const size_t req_size = (length == 0 || remaining > SQUASH_FILE_BUF_SIZE) ? SQUASH_FILE_BUF_SIZE : remaining;
      if (!squash_mapped_file_init_full(&map, fp_out, req_size, true, true)) {
        if (first_block)
          goto nomap;
        else {
          break;
        }
      } else {
        first_block = false;
      }

      res = squash_file_read (file, &map.length, map.data);
      if (res < 0)
        goto cleanup;

      if (res == SQUASH_END_OF_STREAM) {
        assert (map.length == 0);
        res = SQUASH_OK;
        squash_mapped_file_destroy (&map, true);
        goto cleanup;
      }

      squash_mapped_file_destroy (&map, true);
    }
  }

 nomap:
#endif /* defined(SQUASH_MMAP_IO) */

  if (res != SQUASH_OK) {
    file = squash_file_steal_codec_with_options (codec, (stream_type == SQUASH_STREAM_COMPRESS ? fp_out : fp_in), options);
    if (file == NULL) {
      res = squash_error (SQUASH_FAILED);
      goto cleanup;
    }

    data = malloc (SQUASH_FILE_BUF_SIZE);
    if (data == NULL) {
      res = squash_error (SQUASH_MEMORY);
      goto cleanup;
    }

    if (stream_type == SQUASH_STREAM_COMPRESS) {
      while (length == 0 || remaining != 0) {
        const size_t req_size = (length == 0 || remaining > SQUASH_FILE_BUF_SIZE) ? SQUASH_FILE_BUF_SIZE : remaining;

        data_length = SQUASH_FREAD_UNLOCKED(data, 1, req_size, fp_in);
        if (data_length == 0) {
          res = feof (fp_in) ? SQUASH_OK : squash_error (SQUASH_IO);
          goto cleanup;
        }

        res = squash_file_write (file, data_length, data);
        if (res != SQUASH_OK)
          goto cleanup;

        if (remaining != 0) {
          assert (data_length <= remaining);
          remaining -= data_length;
        }
      }
    } else {
      while (length == 0 || remaining != 0) {
        data_length = (length == 0 || remaining > SQUASH_FILE_BUF_SIZE) ? SQUASH_FILE_BUF_SIZE : remaining;
        res = squash_file_read (file, &data_length, data);
        if (res < 0) {
          break;
        } else if (res == SQUASH_PROCESSING) {
          res = SQUASH_OK;
        }

        if (data_length > 0) {
          size_t bytes_written = SQUASH_FWRITE_UNLOCKED(data, 1, data_length, fp_out);
          assert (bytes_written == data_length);
          if (bytes_written == 0) {
            res = squash_error (SQUASH_IO);
            break;
          }

          if (remaining != 0) {
            assert (data_length <= remaining);
            remaining -= data_length;
          }
        }

        if (res == SQUASH_END_OF_STREAM) {
          res = SQUASH_OK;
          break;
        }
      }
    }
  }

 cleanup:

  squash_file_free (file, NULL);
#if defined(SQUASH_MMAP_IO)
  squash_mapped_file_destroy (&map, false);
#endif
  free (data);

  return res;
}

static once_flag squash_splice_detect_once = ONCE_FLAG_INIT;
static int squash_splice_try_mmap = 0;

static void
squash_splice_detect_enable (void) {
  char* ev = getenv ("SQUASH_MAP_SPLICE");

  if (ev == NULL || strcmp (ev, "yes") == 0)
    squash_splice_try_mmap = 2;
  else if (strcmp (ev, "always") == 0)
    squash_splice_try_mmap = 3;
  else if (strcmp (ev, "no") == 0)
    squash_splice_try_mmap = 1;
  else
    squash_splice_try_mmap = 2;
}

struct SquashFileSpliceData {
  FILE* fp_in;
  FILE* fp_out;
  size_t length;
  size_t pos;
  SquashStreamType stream_type;
  SquashCodec* codec;
  SquashOptions* options;
};

static SquashStatus
squash_file_splice_read (size_t* data_length,
                         uint8_t data[SQUASH_ARRAY_PARAM(*data_length)],
                         void* user_data) {
  struct SquashFileSpliceData* ctx = (struct SquashFileSpliceData*) user_data;

  size_t requested;

  if (ctx->stream_type == SQUASH_STREAM_COMPRESS && ctx->length != 0) {
    const size_t remaining = ctx->length - ctx->pos;

    if (remaining == 0) {
      *data_length = 0;
      return SQUASH_END_OF_STREAM;
    }

    requested = (*data_length < remaining) ? *data_length : remaining;
  } else {
    requested = *data_length;
    assert (requested != 0);
  }

  const size_t bytes_read = SQUASH_FREAD_UNLOCKED(data, 1, requested, ctx->fp_in);
  *data_length = bytes_read;
  ctx->pos += bytes_read;

  if (bytes_read == 0) {
    return feof (ctx->fp_in) ? SQUASH_END_OF_STREAM : squash_error (SQUASH_IO);
  } else {
    return SQUASH_OK;
  }
}

static SquashStatus
squash_file_splice_write (size_t* data_length,
                          const uint8_t data[SQUASH_ARRAY_PARAM(*data_length)],
                          void* user_data) {
  struct SquashFileSpliceData* ctx = (struct SquashFileSpliceData*) user_data;

  const size_t requested = *data_length;
  *data_length = SQUASH_FWRITE_UNLOCKED(data, 1, requested, ctx->fp_out);

  return (*data_length == requested) ? SQUASH_OK : squash_error (SQUASH_IO);
}

/* I would care more about the absurd name if this were exposed publicly. */
static SquashStatus
squash_file_splice (FILE* fp_in,
                    FILE* fp_out,
                    size_t length,
                    SquashStreamType stream_type,
                    SquashCodec* codec,
                    SquashOptions* options) {
  struct SquashFileSpliceData data = { fp_in, fp_out, length, 0, stream_type, codec, options };

  return squash_splice_custom_codec_with_options(codec, stream_type, squash_file_splice_write, squash_file_splice_read, &data, length, options);
}

/**
 * @brief compress or decompress the contents of one file to another
 *
 * This function will attempt to compress or decompress the contents
 * of one file to another.  It will attempt to use memory-mapped files
 * in order to reduce memory usage and increase performance, and so
 * should be preferred over writing similar code manually.
 *
 * @param fp_in the input *FILE* pointer
 * @param fp_out the output *FILE* pointer
 * @param length number of bytes (uncompressed) to transfer from @a
 *   fp_in to @a fp_out
 * @param stream_type whether to compress or decompress the data
 * @param codec codec to use
 * @param options options to pass to the codec
 * @returns @ref SQUASH_OK on success, or a negative error code on
 *   failure
 */
SquashStatus
squash_splice_codec_with_options (SquashCodec* codec,
                                  SquashStreamType stream_type,
                                  FILE* fp_out,
                                  FILE* fp_in,
                                  size_t length,
                                  SquashOptions* options) {
  SquashStatus res = SQUASH_FAILED;

  assert (fp_in != NULL);
  assert (fp_out != NULL);
  assert (stream_type == SQUASH_STREAM_COMPRESS || stream_type == SQUASH_STREAM_DECOMPRESS);
  assert (codec != NULL);

  call_once (&squash_splice_detect_once, squash_splice_detect_enable);

  SQUASH_FLOCKFILE(fp_in);
  SQUASH_FLOCKFILE(fp_out);

  if (codec->impl.splice != NULL) {
    res = squash_file_splice (fp_in, fp_out, length, stream_type, codec, options);
  } else {
    if (squash_splice_try_mmap == 3 || (squash_splice_try_mmap == 2 && codec->impl.create_stream == NULL)) {
      res = squash_splice_map (fp_in, fp_out, length, stream_type, codec, options);
    }

    if (res != SQUASH_OK)
      res = squash_splice_stream (fp_in, fp_out, length, stream_type, codec, options);
  }

  SQUASH_FUNLOCKFILE(fp_in);
  SQUASH_FUNLOCKFILE(fp_out);

  return res;
}

#define SQUASH_SPLICE_BUF_SIZE ((size_t) 512)

#if !defined(SQUASH_SPLICE_BUF_SIZE)
#define SQUASH_SPLICE_BUF_SIZE SQUASH_FILE_BUF_SIZE
#endif

struct SquashSpliceLimitedData {
  SquashWriteFunc write_func;
  SquashReadFunc read_func;
  void* user_data;
  SquashStreamType stream_type;
  size_t remaining;
  size_t written;
};

static SquashStatus
squash_splice_custom_limited_write (size_t* data_length, const uint8_t data[SQUASH_ARRAY_PARAM(*data_length)], void* user_data) {
  assert (user_data != NULL);

  struct SquashSpliceLimitedData* ctx = user_data;
  const bool limit_output = ctx->stream_type == SQUASH_STREAM_DECOMPRESS;

  if (limit_output && *data_length > ctx->remaining)
    *data_length = ctx->remaining;

  if (limit_output && *data_length == 0) {
    return SQUASH_END_OF_STREAM;
  }

  SquashStatus res = ctx->write_func (data_length, data, ctx->user_data);
  if (res < 0)
    return res;

  if (limit_output)
    ctx->remaining -= *data_length;
  ctx->written += *data_length;

  return res;
}

static SquashStatus
squash_splice_custom_limited_read (size_t* data_length, uint8_t data[SQUASH_ARRAY_PARAM(*data_length)], void* user_data) {
  assert (user_data != NULL);

  struct SquashSpliceLimitedData* ctx = user_data;
  const bool limit_input = ctx->stream_type == SQUASH_STREAM_COMPRESS;

  if (ctx->remaining == 0) {
    *data_length = 0;
    return SQUASH_END_OF_STREAM;
  }

  if (limit_input && *data_length > ctx->remaining)
    *data_length = ctx->remaining;

  SquashStatus res = ctx->read_func (data_length, data, ctx->user_data);
  if (limit_input && res > 0)
    ctx->remaining -= *data_length;

  return res;
}

SquashStatus
squash_splice_custom_codec_with_options (SquashCodec* codec,
                                         SquashStreamType stream_type,
                                         SquashWriteFunc write_cb,
                                         SquashReadFunc read_cb,
                                         void* user_data,
                                         size_t length,
                                         SquashOptions* options) {
  SquashStatus res = SQUASH_OK;
  const bool limit_input = (stream_type == SQUASH_STREAM_COMPRESS && length != 0);
  const bool limit_output = (stream_type == SQUASH_STREAM_DECOMPRESS && length != 0);

  if (codec->impl.splice != NULL) {
    if (length == 0) {
      return codec->impl.splice (codec, options, stream_type, squash_file_splice_read, squash_file_splice_write, user_data);
    } else {
      /* We need to limit the amount of data input (for compression)
         and output (for decompression), so we some wrapper
         callbacks. */
      struct SquashSpliceLimitedData ctx = {
        write_cb,
        read_cb,
        user_data,
        stream_type,
        length,
        0
      };
      return codec->impl.splice (codec, options, stream_type, squash_splice_custom_limited_read, squash_splice_custom_limited_write, &ctx);
    }
  } else if (codec->impl.process_stream) {
    SquashStream* stream = squash_stream_new_codec_with_options(codec, stream_type, options);
    if (stream == NULL)
      return squash_error (SQUASH_FAILED);

    uint8_t* const in_buf = malloc (SQUASH_SPLICE_BUF_SIZE);
    uint8_t* const out_buf = malloc (SQUASH_SPLICE_BUF_SIZE);

    if (in_buf == NULL || out_buf == NULL) {
      res = squash_error (SQUASH_MEMORY);
      goto cleanup_stream;
    }

    bool eof = false;

    do {
      stream->next_in = in_buf;
      if (limit_input) {
        stream->avail_in = length - stream->total_in;
        if (stream->avail_in > SQUASH_SPLICE_BUF_SIZE)
          stream->avail_in = SQUASH_SPLICE_BUF_SIZE;
      } else {
        stream->avail_in = SQUASH_SPLICE_BUF_SIZE;
      }
      res = read_cb (&(stream->avail_in), in_buf, user_data);

      if (res < 0)
        break;
      else if (res == SQUASH_END_OF_STREAM)
        eof = true;

      do {
        stream->next_out = out_buf;
        stream->avail_out = SQUASH_SPLICE_BUF_SIZE;

        if (eof) {
          res = squash_stream_finish (stream);
        } else {
          res = squash_stream_process (stream);
        }

        if (res < 0)
          break;

        size_t write_remaining = SQUASH_SPLICE_BUF_SIZE - stream->avail_out;
        if (limit_output && stream->total_out > length) {
          const size_t overrun = stream->total_out - length;
          assert (overrun < SQUASH_SPLICE_BUF_SIZE);
          write_remaining -= overrun;
          res = SQUASH_OK;
          eof = true;
        }

        while (write_remaining != 0) {
          size_t written = write_remaining;
          SquashStatus res2 = write_cb (&written, out_buf, user_data);
          if (res2 < 0)
            break;

          assert (write_remaining >= written);
          write_remaining -= written;
        }

      } while (res == SQUASH_PROCESSING);
    } while (res == SQUASH_OK && !eof);

    if (res == SQUASH_END_OF_STREAM)
      res = SQUASH_OK;

  cleanup_stream:
    free (in_buf);
    free (out_buf);
  } else {
    SquashBuffer* buffer = squash_buffer_new (0);
    bool eof = false;
    uint8_t* out_data = NULL;
    size_t out_data_size = 0;

    /* Read all data into `buffer'. */
    do {
      const size_t old_size = buffer->length;
      const size_t read_request = limit_input ? (length - old_size): SQUASH_SPLICE_BUF_SIZE;

      if (!squash_buffer_set_size (buffer, old_size + read_request))
        return squash_error (SQUASH_MEMORY);

      size_t bytes_read = read_request;
      res = read_cb (&bytes_read, buffer->data + old_size, user_data);
      if (SQUASH_UNLIKELY(res < 0))
        return res;

      assert (bytes_read <= read_request);

      buffer->length = old_size + bytes_read;

      if (res == SQUASH_END_OF_STREAM || (limit_input && buffer->length == length))
        eof = true;
    } while (!eof);

    /* Process (compress or decompress) the data. */
    if (stream_type == SQUASH_STREAM_COMPRESS) {
      out_data_size = squash_codec_get_max_compressed_size (codec, buffer->length);
      out_data = malloc (out_data_size);
      if (out_data == NULL) {
        res = squash_error (SQUASH_MEMORY);
        goto cleanup_buffer;
      }

      res = squash_codec_compress_with_options (codec, &out_data_size, out_data, buffer->length, buffer->data, options);
      if (res != SQUASH_OK)
        goto cleanup_buffer;
    } else {
      const bool knows_uncompressed =
        (squash_codec_get_info (codec) & SQUASH_CODEC_INFO_KNOWS_UNCOMPRESSED_SIZE) == SQUASH_CODEC_INFO_KNOWS_UNCOMPRESSED_SIZE;

      if (knows_uncompressed) {
        out_data_size = squash_codec_get_uncompressed_size(codec, buffer->length, buffer->data);
        if (out_data_size == 0) {
          res = squash_error (SQUASH_INVALID_BUFFER);
          goto cleanup_buffer;
        }

        out_data = malloc (out_data_size);
        if (out_data == NULL) {
          res = squash_error (SQUASH_MEMORY);
          goto cleanup_buffer;
        }

        res = squash_codec_decompress_with_options (codec, &out_data_size, out_data, buffer->length, buffer->data, options);
      } else {
        /* TODO: I think this is the third time I've written this code.
           I should probably add a squash_codec_decompress_dynamic (or
           something) which just decompresses an input buffer to a
           SquashBuffer. */
        out_data_size = squash_npot (buffer->length) << 3;
        assert (out_data == NULL);
        do {
          size_t c = out_data_size;
          free (out_data);
          out_data = malloc (c);
          if (out_data == NULL) {
            res = squash_error (SQUASH_MEMORY);
            goto cleanup_buffer;
          }

          res = squash_codec_decompress_with_options(codec, &c, out_data, buffer->length, buffer->data, options);
          if (res == SQUASH_BUFFER_FULL) {
            out_data_size <<= 1;
          } else if (res == SQUASH_OK) {
            out_data_size = c;
          }
        } while (res == SQUASH_BUFFER_FULL);
      }
    }

    {
      size_t bytes_written = 0;
      if (limit_output && out_data_size > length)
        out_data_size = length;

      do {
        size_t wlen = out_data_size - bytes_written;
        res = write_cb (&wlen, out_data + bytes_written, user_data);
        if (res != SQUASH_OK) {
          break;
        }
        bytes_written += wlen;
      } while (bytes_written != out_data_size);
    }

  cleanup_buffer:
    squash_buffer_free (buffer);
    free (out_data);
  }

  return res;
}

/**
 * @}
 */