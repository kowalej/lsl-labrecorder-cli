#include "lslstreamwriter.h"
#include <iostream>


std::string replace_all(std::string str, const std::string &from, const std::string &to) {
	size_t start_pos = 0;
	while ((start_pos = str.find(from, start_pos)) != std::string::npos) {
		str.replace(start_pos, from.length(), to);
		start_pos += to.length(); // Handles case where 'to' is a substring of 'from'
	}
	return str;
}

void write_timestamp(std::ostream &out, double ts) {
	// [TimeStampBytes] (0 for no time stamp).
	if (ts == 0)
		out.put(0);
	else {
		// [TimeStampBytes].
		out.put(8);
		// [TimeStamp].
		write_little_endian(out, ts);
	}
}

LSLStreamWriter::LSLStreamWriter(const std::string &filename, file_type_t filetype)
	: filename_(filename), filetype_(filetype) {}

void LSLStreamWriter::_write_chunk(
	chunk_tag_t tag, const std::string &content, const streamid_t *streamid_p) {
	// Write the chunk header for XDF format only.
	if (filetype_ == file_type_t::xdf) { _write_chunk_header(tag, content.length(), streamid_p); }
	// [Content].
	*_get_file(streamid_p, tag) << content;
}

void LSLStreamWriter::_write_chunk_header(
	chunk_tag_t tag, std::size_t len, const streamid_t *streamid_p) {
	outfile_t *file = _get_file(streamid_p, tag);

	// Optional: [StreamId].
	if (streamid_p) {
		len += sizeof(chunk_tag_t);
		if (streamid_p) len += sizeof(streamid_t);

		// [Length] (variable-length integer, content + 2 bytes for the tag
		// + 4 bytes if the streamid is being written.
		write_varlen_int(*file, len);
	}

	// Always Write: [Tag].
	write_little_endian(*file, static_cast<uint16_t>(tag));

	// Optional: [StreamId].
	if (streamid_p) write_little_endian(*file, *streamid_p);
}

void LSLStreamWriter::init_stream_file(streamid_t streamid, std::string stream_name) {
	std::lock_guard<std::mutex> lock(*_get_write_mutex(&streamid));

	// XDF special handling.
	if (filetype_ == file_type_t::xdf && !xdf_created_) {
#ifndef XDFZ_SUPPORT
		xdf_file_ = outfile_t(filename_, std::ios::binary | std::ios::trunc);
#endif
#ifdef XDFZ_SUPPORT
		if (boost::iends_with(filename_, ".xdfz")) {
			xdf_file.push(boost::iostreams::zlib_compressor());
		}
		xdf_file.push(
			boost::iostreams::file_descriptor_sink(filename_, std::ios::binary | std::ios::trunc));
#endif
		xdf_created_ = true;
		xdf_file_ << "XDF:";
		_write_chunk(
			chunk_tag_t::fileheader, "<?xml version=\"1.0\"?><info><version>1.0</version></info>");
	}

	// CSV setup.
	else if (filetype_ == file_type_t::csv) {
		auto stream_name_safe = replace_all(stream_name, ":", ".");
		std::string csv_filename =
			replace_all(filename_, ".csv", " - " + stream_name_safe + ".data.csv");
		std::string meta_filename =
			replace_all(filename_, ".csv", " - " + stream_name_safe + ".meta.xml");
		data_files_[streamid] = outfile_t(csv_filename, std::ios::binary | std::ios::trunc); // Create a data file for each stream.
		meta_files_[streamid] = outfile_t(meta_filename, std::ios::binary | std::ios::trunc); // Create a meta data file for each stream.
		file_mutex_.emplace(streamid, std::make_unique<std::mutex>()); // Create a write lock for each stream.
		_write_chunk(chunk_tag_t::fileheader,
			"<?xml version=\"1.0\"?><info><version>1.0</version></info>\n", &streamid);
	}
}

void LSLStreamWriter::write_stream_header(streamid_t streamid, const std::string &content) {
	std::lock_guard<std::mutex> lock(*_get_write_mutex(&streamid));
	_write_chunk(chunk_tag_t::streamheader, content, &streamid);
}

void LSLStreamWriter::write_stream_footer(streamid_t streamid, const std::string &content) {
	std::lock_guard<std::mutex> lock(*_get_write_mutex(&streamid));
	_write_chunk(chunk_tag_t::streamfooter, content, &streamid);
}

void LSLStreamWriter::write_stream_offset(streamid_t streamid, double now, double offset) {
	if (filetype_ == file_type_t::xdf) {
		std::lock_guard<std::mutex> lock(*_get_write_mutex(&streamid));
		const auto len = sizeof(now) + sizeof(offset);
		outfile_t *file = _get_file(&streamid, chunk_tag_t::clockoffset);

		// Write the chunk header for XDF format only.
		_write_chunk_header(chunk_tag_t::clockoffset, len, &streamid);
	
		// [CollectionTime].
		write_little_endian(*file, now - offset);
		// [OffsetValue].
		write_little_endian(*file, offset);
	}
}

void LSLStreamWriter::write_boundary_chunk() {
	// Boundary chunk only required for XDF.
	if (filetype_ == file_type_t::xdf) {
		std::lock_guard<std::mutex> lock(*_get_write_mutex(nullptr));
		// The signature of the boundary chunk (next chunk begins right after this).
		const uint8_t boundary_uuid[] = {0x43, 0xA5, 0x46, 0xDC, 0xCB, 0xF5, 0x41, 0x0F, 0xB3, 0x0E,
			0xD5, 0x46, 0x73, 0x83, 0xCB, 0xE4};
		_write_chunk_header(chunk_tag_t::boundary, sizeof(boundary_uuid));
		write_sample_values(xdf_file_, boundary_uuid, sizeof(boundary_uuid));
	}
}
