#include "lslstreamwriter.h"
#include <iostream>


void write_timestamp(std::ostream &out, double ts) {
	// [TimeStampBytes] (0 for no time stamp)
	if (ts == 0)
		out.put(0);
	else {
		// [TimeStampBytes]
		out.put(8);
		// [TimeStamp]
		write_little_endian(out, ts);
	}
}

LSLStreamWriter::LSLStreamWriter(const std::string &filename,
	file_type_t filetype, const std::vector<lsl::stream_info> &streams)
	: filetype_(filetype) {

	// XDF special handling.
	if (filetype == file_type_t::xdf) {
#ifndef XDFZ_SUPPORT
		xdf_file_ = outfile_t(filename, std::ios::binary | std::ios::trunc);
#endif
#ifdef XDFZ_SUPPORT
		if (boost::iends_with(filename, ".xdfz")) {
			xdf_file.push(boost::iostreams::zlib_compressor());
		}
		xdf_file.push(
			boost::iostreams::file_descriptor_sink(filename, std::ios::binary | std::ios::trunc));
#endif
		xdf_file_ << "XDF:";
		_write_chunk(
			chunk_tag_t::fileheader, "<?xml version=\"1.0\"?><info><version>1.0</version></info>");
	}

	else {
		for (const auto &stream : streams) {
			std::string stream_name = stream.name();
			std::string csv_filename = filename + stream_name + ".csv";
			std::string meta_filename = filename + stream_name + ".csv";
			//data_files_.emplace(stream, outfile_t(filename, std::ios::binary | std::ios::trunc));
			//meta_files_.emplace(stream, outfile_t(filename, std::ios::binary | std::ios::trunc));
		}
	}
}

outfile_t* LSLStreamWriter::_get_data_file(const streamid_t *streamid_p) {
	if (filetype_ == file_type_t::xdf) { 
		return &xdf_file_;
	} else {
		return &data_files_[streamid_p];
	}
}

outfile_t *LSLStreamWriter::_get_meta_file(const streamid_t *streamid_p) {
	if (filetype_ != file_type_t::xdf) {
		return &meta_files_[streamid_p];
	}0
}


void LSLStreamWriter::_write_chunk(
	chunk_tag_t tag, const std::string &content, const streamid_t *streamid_p) {
	// Write the chunk header
	_write_chunk_header(tag, content.length(), streamid_p);
	// [Content]
	*_get_data_file(streamid_p) << content;
}

void LSLStreamWriter::_write_chunk_header(
	chunk_tag_t tag, std::size_t len, const streamid_t *streamid_p) {
	len += sizeof(chunk_tag_t);
	if (streamid_p) len += sizeof(streamid_t);

	// [Length] (variable-length integer, content + 2 bytes for the tag
	// + 4 bytes if the streamid is being written
	write_varlen_int(*_get_data_file(streamid_p), len);
	// [Tag]
	write_little_endian(*_get_data_file(streamid_p), static_cast<uint16_t>(tag));
	// Optional: [StreamId]
	if (streamid_p) write_little_endian(*_get_data_file(streamid_p), *streamid_p);
}

void LSLStreamWriter::write_stream_header(streamid_t streamid, const std::string &content) {
	std::lock_guard<std::mutex> lock(write_mut);
	_write_chunk(chunk_tag_t::streamheader, content, &streamid);
}

void LSLStreamWriter::write_stream_footer(streamid_t streamid, const std::string &content) {
	std::lock_guard<std::mutex> lock(write_mut);
	_write_chunk(chunk_tag_t::streamfooter, content, &streamid);
}

void LSLStreamWriter::write_stream_offset(streamid_t streamid, double now, double offset) {
	std::lock_guard<std::mutex> lock(write_mut);
	const auto len = sizeof(now) + sizeof(offset);
	_write_chunk_header(chunk_tag_t::clockoffset, len, &streamid);
	// [CollectionTime]
	write_little_endian(*_get_data_file(&streamid), now - offset);
	// [OffsetValue]
	write_little_endian(*_get_data_file(&streamid), offset);
}

void LSLStreamWriter::write_boundary_chunk() {
	std::lock_guard<std::mutex> lock(write_mut);
	// the signature of the boundary chunk (next chunk begins right after this)
	const uint8_t boundary_uuid[] = {0x43, 0xA5, 0x46, 0xDC, 0xCB, 0xF5, 0x41, 0x0F, 0xB3, 0x0E,
		0xD5, 0x46, 0x73, 0x83, 0xCB, 0xE4};
	_write_chunk_header(chunk_tag_t::boundary, sizeof(boundary_uuid));
	write_sample_values(*_get_data_file(nullptr), boundary_uuid, sizeof(boundary_uuid));
}
