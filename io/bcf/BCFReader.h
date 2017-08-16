#ifndef BCFREADER_H_
#define BCFREADER_H_

#include "../BasicBuffer.h"
#include "../BGZFController.h"

namespace Tomahawk {
namespace IO {

#pragma pack(1)
struct BCFEntryBody{
	typedef BCFEntryBody self_type;

	BCFEntryBody(); // disallow ctor and dtor
	~BCFEntryBody();

	friend std::ostream& operator<<(std::ostream& os, const self_type& header){
		os << "l_shared\t" << (U32)header.l_shared << '\n';
		os << "l_indiv\t" << (U32)header.l_indiv << '\n';
		os << "CHROM\t" << (U32)header.CHROM << '\n';
		os << "POS\t" << (U32)header.POS << '\n';
		os << "rlen\t" << (S32)header.rlen << '\n';
		os << "QUAL\t" << (U32)header.QUAL << '\n';
		os << "n_allele\t" << (U32)header.n_allele << '\n';
		os << "n_info\t" << (U16)header.n_info << '\n';
		os << "n_fmt\t" << (U32)header.n_fmt << '\n';
		os << "n_sample\t" << (U32)header.n_sample;

		return os;
	}

	U32 l_shared;
	U32 l_indiv;
	S32 CHROM;
	S32 POS;
	S32 rlen;
	float QUAL;
	U32 n_info: 16, n_allele: 16;
	U32 n_sample: 8, n_fmt: 24;
};

#pragma pack(1)
struct BCFAtomicBase{
	BYTE low: 4, high: 4;
};

class BCFReader{
	typedef BCFReader self_type;
	typedef IO::BasicBuffer buffer_type;
	typedef IO::BGZFController bgzf_controller_type;
	typedef BGZFHeader bgzf_type;

public:
	BCFReader() : filesize(0){}
	BCFReader(const std::string file);
	~BCFReader(){}

	bool nextBlock(void){
		// Stream died
		if(!this->stream.good()){
			std::cerr << Tomahawk::Helpers::timestamp("ERROR", "BCF") << "Stream died!" << std::endl;
			return false;
		}

		// EOF
		if(this->stream.tellg() == this->filesize){
			//std::cerr << "eof" << std::endl;
			return false;
		}

		buffer.resize(sizeof(bgzf_type));
		this->stream.read(&buffer.data[0], Constants::BGZF_BLOCK_HEADER_LENGTH);
		const bgzf_type* h = reinterpret_cast<const bgzf_type*>(&buffer.data[0]);
		buffer.pointer = Constants::BGZF_BLOCK_HEADER_LENGTH;
		if(!h->Validate()){
			std::cerr << Tomahawk::Helpers::timestamp("ERROR", "BCF") << "Failed to validate!" << std::endl;
			std::cerr << *h << std::endl;
			return false;
		}

		buffer.resize(h->BSIZE); // make sure all data will fit

		// Recast because if buffer is resized then the pointer address is incorrect
		// resulting in segfault
		h = reinterpret_cast<const bgzf_type*>(&buffer.data[0]);

		this->stream.read(&buffer.data[Constants::BGZF_BLOCK_HEADER_LENGTH], (h->BSIZE + 1) - Constants::BGZF_BLOCK_HEADER_LENGTH);
		if(!this->stream.good()){
			std::cerr << Tomahawk::Helpers::timestamp("ERROR", "BCF") << "Truncated file..." << std::endl;
			return false;
		}

		buffer.pointer = h->BSIZE + 1;
		const U32 uncompressed_size = *reinterpret_cast<const U32*>(&buffer[buffer.pointer -  sizeof(U32)]);
		output_buffer.resize(uncompressed_size);
		this->output_buffer.reset();

		if(!this->bgzf_controller.Inflate(buffer, output_buffer)){
			std::cerr << Tomahawk::Helpers::timestamp("ERROR", "BCF") << "Failed inflate!" << std::endl;
			return false;
		}

		// BGZF EOF marker
		if(this->output_buffer.size() == 0)
			return false;

		// Reset buffer
		this->buffer.reset();

		return true;
	}

	bool parseHeader(void){
		if(this->output_buffer.size() == 0){
			std::cerr << "no buffer" << std::endl;
			return false;
		}

		if(strncmp(&this->output_buffer.data[0], "BCF\2\2", 5) != 0){ // weird: should be BCF/2/1
			std::cerr << (int)this->output_buffer[3] << '\t' << (int)this->output_buffer[4] << std::endl;
			std::cerr << "failed to validate" << std::endl;
			return false;
		}

		const U32 l_text = *reinterpret_cast<const U32* const>(&this->output_buffer[5]) + 4;
		this->header_buffer.resize(l_text);

		if(l_text - 5 < this->output_buffer.size()){
			this->header_buffer.Add(&this->output_buffer[5], l_text);
			return true;
		}

		U32 head_read = this->output_buffer.size() - 5;
		this->header_buffer.Add(&this->output_buffer[5], this->output_buffer.size() - 5);

		U32 p = 0;
		while(this->nextBlock()){
			if(head_read + this->output_buffer.size() >= l_text){
				std::cerr << "remainder: " << l_text - head_read << " and data: " << this->output_buffer.size() << std::endl;
				this->header_buffer.Add(&this->output_buffer[0], l_text - head_read);
				p = l_text - head_read;
				break;
			}
			head_read += this->output_buffer.size();
			this->header_buffer.Add(&this->output_buffer[0], this->output_buffer.size());
		}

		//std::cerr << this->header_buffer;
		//std::cerr << "next" << std::endl;
		const BCFEntryBody& b = *reinterpret_cast<const BCFEntryBody* const>(&this->output_buffer[p]);
		std::cerr << b << std::endl;
		p += sizeof(BCFEntryBody);
		std::cerr << (int)this->output_buffer[p] << std::endl;
		const BCFAtomicBase& s = *reinterpret_cast<const BCFAtomicBase* const>(&this->output_buffer[p]);
		std::cerr << (int)s.high << '\t' << (int)s.low << std::endl;
		p += 1;
		for(U32 i = 0; i < s.high; ++i)
			std::cerr << i << ' ' << this->output_buffer[p+i] << std::endl;
		std::cerr << std::endl;

		p+= s.high;
		const BCFAtomicBase& s2 = *reinterpret_cast<const BCFAtomicBase* const>(&this->output_buffer[p]);
		std::cerr << "Typed allele1: " << (int)s2.high << '\t' << (int)s2.low << std::endl;

		const BCFAtomicBase& s3 = *reinterpret_cast<const BCFAtomicBase* const>(&this->output_buffer[p+2]);
		std::cerr << "Typed allele1: " << (int)s3.high << '\t' << (int)s3.low << std::endl;
		p += 3;

		return true;
	}

	bool open(const std::string input){
		this->stream.open(input, std::ios::binary | std::ios::in | std::ios::ate);
		if(!this->stream.good()){
			std::cerr << Tomahawk::Helpers::timestamp("ERROR", "BCF") << "Failed to open file: " << input << std::endl;
			return false;
		}

		this->filesize = this->stream.tellg();
		this->stream.seekg(0);

		if(!this->stream.good()){
			std::cerr << Tomahawk::Helpers::timestamp("ERROR", "BCF") << "Bad stream!" << std::endl;
			return false;
		}

		if(!this->nextBlock()){
			std::cerr << "failed ot get first block" << std::endl;
			return false;
		}

		if(!this->parseHeader()){
			std::cerr << "failed to parse bcf header" << std::endl;
			return false;
		}

		return true;
	}

public:
	std::ifstream stream;
	U64 filesize;
	buffer_type buffer;
	buffer_type output_buffer;
	buffer_type header_buffer;
	bgzf_controller_type bgzf_controller;
};

}
}



#endif /* BCFREADER_H_ */