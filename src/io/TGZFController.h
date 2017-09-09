#ifndef GZCONTROLLER_H_
#define GZCONTROLLER_H_

#include <fstream>

#include "../support/helpers.h"
#include "../third_party/zlib/zconf.h"
#include "../third_party/zlib/zlib.h"
#include "BasicBuffer.h"
#include "GZFHeader.h"

namespace Tomahawk{
namespace IO{

class TGZFController{
private:
	typedef TGZFController self_type;

protected:
	typedef IO::BasicBuffer buffer_type;
	typedef TGZFHeader header_type;

public:
	TGZFController();
	TGZFController(const char* data, const U32 length);
	TGZFController(const U32 largest_block_size);
	~TGZFController();

	void Clear();
	bool Inflate(buffer_type& input, buffer_type& output, const header_type& header) const;
	bool Inflate(buffer_type& input, buffer_type& output) const;
	bool InflateBlock(std::ifstream& stream, buffer_type& input);

	bool Deflate(const buffer_type& buffer);
	bool Deflate(buffer_type& meta, buffer_type& rle);

	friend std::ostream& operator<<(std::ostream& stream, const self_type& entry){
		stream.write(entry.buffer.data, entry.buffer.pointer);
		return stream;
	}

private:
	bool __Inflate(buffer_type& input, buffer_type& output, const header_type& header) const;

public:
	buffer_type buffer;
};

class TGZFControllerStream : public TGZFController{
protected:
	enum TGZF_STATE {TGZF_OK, TGZF_HEADER, TGZF_END, TGZF_INIT, TGZF_ERROR};

public:
	TGZFControllerStream();
	~TGZFControllerStream();

	bool Inflate(std::ifstream& stream, const BYTE* output, const U32& avail_out, U32& return_size);

	void reset(void){
		this->total_out = 0;
		this->bytes_read = 0;
		this->BSIZE = 0;
		this->STATE = TGZF_STATE::TGZF_INIT;
	}

protected:
	bool InflateOpen(std::ifstream& stream);
	bool __Inflate(std::ifstream& stream, const BYTE* output, const U32 avail_out, U32& return_size);

protected:
	TGZF_STATE STATE;
	U32 chunk_size;
	U32 total_out;
	U32 bytes_read;
	U32 BSIZE;
	z_stream d_stream;
};

template <class T>
class TGZFEntryIterator : public TGZFControllerStream{
	typedef TGZFControllerStream parent_type;

public:
	TGZFEntryIterator(std::ifstream& stream, const U32 n_entries);
	TGZFEntryIterator(std::ifstream& stream, const U32 n_entries, const U64 from, const U64 to);
	~TGZFEntryIterator();

	bool nextEntry(const T*& entry);

//private:
//	void reset(void){ this->pointer = 0; this->n_entries = 0; this->entries = nullptr; }

private:
	U32 pointer;
	U32 n_entries;
	U32 chunk_size;
	// Offset must equal a TGZF boundary
	// no checks are made
	U64 IO_start_offset; // start TGZF block offset
	U64 IO_end_offset; // end TGZF block offset
	std::ifstream& stream;
	buffer_type output_buffer;
	const T* entries;
};

template <class T>
TGZFEntryIterator<T>::TGZFEntryIterator(std::ifstream& stream, const U32 n_entries) :
	pointer(0),
	n_entries(0),
	chunk_size(n_entries*sizeof(T)),
	IO_start_offset(0),
	IO_end_offset(std::numeric_limits<U64>::max()),
	stream(stream),
	output_buffer(n_entries*sizeof(T)),
	entries(nullptr)
{}

template <class T>
TGZFEntryIterator<T>::TGZFEntryIterator(std::ifstream& stream, const U32 n_entries, const U64 from, const U64 to) :
	pointer(0),
	n_entries(0),
	chunk_size(n_entries*sizeof(T)),
	IO_start_offset(from),
	IO_end_offset(to),
	stream(stream),
	output_buffer(n_entries*sizeof(T)),
	entries(nullptr){}

template <class T>
TGZFEntryIterator<T>::~TGZFEntryIterator(){ this->output_buffer.deleteAll(); }

template <class T>
bool TGZFEntryIterator<T>::nextEntry(const T*& entry){
	if(this->pointer == this->n_entries){
		//check if allowed to proceed
		if(this->STATE == TGZF_STATE::TGZF_END){
			this->stream.seekg(IO::Constants::TGZF_BLOCK_FOOTER_LENGTH, std::ios::cur);

			if(this->stream.tellg() == this->IO_end_offset)
				return false;

			this->reset(); // reset state
		}

		U32 ret_size = 0;
		if(!parent_type::Inflate(this->stream, (BYTE*)&output_buffer.data[0], this->chunk_size, ret_size)){
			if(this->STATE != TGZF_STATE::TGZF_END){
				std::cerr << "invalid state" << std::endl;
				exit(1);
			}
		}

		if(ret_size % sizeof(T) != 0){
			std::cerr << "impossible: " << ret_size % sizeof(T) << '\t' << ret_size << '/' << this->chunk_size << '\t' << "state: " << this->STATE << " size: " << sizeof(T) << std::endl;
			exit(1);
		}

		if(ret_size == 0){
			std::cerr << "returned nothing: " << this->STATE << std::endl;
			if(this->STATE == TGZF_STATE::TGZF_END){
				this->stream.seekg(IO::Constants::TGZF_BLOCK_FOOTER_LENGTH, std::ios::cur);

				if(this->stream.tellg() == this->IO_end_offset)
					return false;

				this->reset(); // reset state
			}

			if(!parent_type::Inflate(this->stream, (BYTE*)&output_buffer.data[0], this->chunk_size, ret_size)){
				if(this->STATE != TGZF_STATE::TGZF_END){
					std::cerr << "invalid state" << std::endl;
					exit(1);
				}
			}
			if(ret_size == 0){
				std::cerr << "impossible" << std::endl;
				exit(1);
			}

		}

		this->output_buffer.pointer = ret_size;
		this->n_entries = ret_size / sizeof(T);
		this->pointer = 0;
		this->entries = reinterpret_cast<const T*>(this->output_buffer.data);
	}

	entry = &this->entries[this->pointer++];
	return true;
}

}
}

#endif /* GZCONTROLLER_H_ */
