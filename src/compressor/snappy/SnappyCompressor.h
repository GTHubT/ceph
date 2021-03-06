// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*-
// vim: ts=8 sw=2 smarttab
/*
 * Ceph - scalable distributed file system
 *
 * Copyright (C) 2015 Haomai Wang <haomaiwang@gmail.com>
 *
 * This is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License version 2.1, as published by the Free Software 
 * Foundation.  See file COPYING.
 *
 */

#ifndef CEPH_SNAPPYCOMPRESSOR_H
#define CEPH_SNAPPYCOMPRESSOR_H

#include <snappy.h>
#include <snappy-sinksource.h>
#include "common/config.h"
#include "compressor/Compressor.h"
#include "include/buffer.h"

class CEPH_BUFFER_API BufferlistSource : public snappy::Source {
  bufferlist::const_iterator pb;
  size_t remaining;

 public:
  explicit BufferlistSource(bufferlist::const_iterator _pb, size_t _input_len)
    : pb(_pb),
      remaining(_input_len) {
    remaining = std::min(remaining, (size_t)pb.get_remaining());
  }
  size_t Available() const override {
    return remaining;
  }
  const char *Peek(size_t *len) override {
    const char *data = NULL;
    *len = 0;
    size_t avail = Available();
    if (avail) {
      auto ptmp = pb;
      *len = ptmp.get_ptr_and_advance(avail, &data);
    }
    return data;
  }
  void Skip(size_t n) override {
    ceph_assert(n <= remaining);
    pb.advance(n);
    remaining -= n;
  }

  bufferlist::const_iterator get_pos() const {
    return pb;
  }
};

class SnappyCompressor : public Compressor {
 public:
  SnappyCompressor(CephContext* cct) : Compressor(COMP_ALG_SNAPPY, "snappy") {
#ifdef HAVE_QATZIP
    if (cct->_conf->qat_compressor_enabled && qat_accel.init("snappy"))
      qat_enabled = true;
    else
      qat_enabled = false;
#endif
  }

  int compress(const bufferlist &src, bufferlist &dst) override {
#ifdef HAVE_QATZIP
    if (qat_enabled)
      return qat_accel.compress(src, dst);
#endif
    BufferlistSource source(const_cast<bufferlist&>(src).begin(), src.length());
    bufferptr ptr = buffer::create_small_page_aligned(
      snappy::MaxCompressedLength(src.length()));
    snappy::UncheckedByteArraySink sink(ptr.c_str());
    snappy::Compress(&source, &sink);
    dst.append(ptr, 0, sink.CurrentDestination() - ptr.c_str());
    return 0;
  }

  int decompress(const bufferlist &src, bufferlist &dst) override {
#ifdef HAVE_QATZIP
    if (qat_enabled)
      return qat_accel.decompress(src, dst);
#endif
    auto i = src.begin();
    return decompress(i, src.length(), dst);
  }

  int decompress(bufferlist::const_iterator &p,
		 size_t compressed_len,
		 bufferlist &dst) override {
#ifdef HAVE_QATZIP
    if (qat_enabled)
      return qat_accel.decompress(p, compressed_len, dst);
#endif
    snappy::uint32 res_len = 0;
    BufferlistSource source_1(p, compressed_len);
    if (!snappy::GetUncompressedLength(&source_1, &res_len)) {
      return -1;
    }
    BufferlistSource source_2(p, compressed_len);
    bufferptr ptr(res_len);
    if (snappy::RawUncompress(&source_2, ptr.c_str())) {
      p = source_2.get_pos();
      dst.append(ptr);
      return 0;
    }
    return -2;
  }
};

#endif
