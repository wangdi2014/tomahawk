#ifndef TOMAHAWK_TOMAHAWKREADER_H_
#define TOMAHAWK_TOMAHAWKREADER_H_

#include <string>
#include <fstream>
#include <algorithm>
#include <bitset>

#include "../algorithm/load_balancer_ld.h"
#include "../interface/progressbar.h"
#include "../support/MagicConstants.h"
#include "../io/compression/TGZFController.h"
#include "../io/compression/GZFConstants.h"
#include "../interface/timer.h"
#include "meta_entry.h"
#include "twk_reader_implementation.h"
#include "ld_calculation_slave.h"
#include "TomahawkCalcParameters.h"
#include "../index/index.h"
#include "genotype_container_reference.h"
#include "../index/tomahawk_header.h"

namespace Tomahawk {

// TomahawkReader class simply reads compressed data from disk
class TomahawkReader {
	typedef TomahawkCalcParameters     parameter_type;
	typedef Totempole::IndexEntry      totempole_entry;
	typedef TomahawkHeader             header_type;
	typedef Index                      index_type;
	typedef IO::BasicBuffer            buffer_type;
	typedef IO::TGZFController         tgzf_controller_type;
	typedef Totempole::Footer          footer_type;

public:
	// Used to keep track of char pointer offsets in buffer
	// and what Totempole entry is associated with that position
	struct DataOffsetPair{
		DataOffsetPair(const char* data, const U64 l_buffer, const totempole_entry& entry) : entry(entry), l_buffer(l_buffer), data(data){}
		~DataOffsetPair(){}

		const totempole_entry& entry;
		const U64 l_buffer;
		const char* data;
	};

public:
	TomahawkReader();
	~TomahawkReader();

	bool Open(const std::string input);

	// Reader functions
	bool getBlocks(void);
	bool getBlocks(std::vector<U32>& blocks);
	bool getBlocks(std::vector< std::pair<U32, U32> >& blocks);
	bool getBlock(const U32 blockID);

    inline footer_type& getFooter(void){ return(this->footer_); }
    inline const footer_type& getFooter(void) const{ return(this->footer_); }

	// Output functions
	bool outputBlocks(std::vector<U32>& blocks);
	bool outputBlocks();

	inline const BYTE& getBitWidth(void) const{ return(this->bit_width_); }
	inline const DataOffsetPair& getOffsetPair(const U32 p) const{ return(this->blockDataOffsets_[p]); }
	inline const size_t DataOffsetSize(void) const{ return(this->blockDataOffsets_.size()); }
	inline void setDropGenotypes(const bool yes){ this->dropGenotypes = yes; }
	inline void setShowHeader(const bool yes){ this->showHeader = yes; }

	inline const index_type& getIndex(void) const{ return(*this->index_); }
	inline index_type& getIndex(void){ return(*this->index_); }
	inline const header_type& getHeader(void) const{ return(this->header_); }
	inline header_type& getHeader(void){ return(this->header_); }
	inline index_type* getIndexPointer(void){ return(this->index_); }

private:
	void DetermineBitWidth(void);
	template <class T> bool outputBlock(const U32 blockID);
	template <class T> bool WriteBlock(const char* data, const U32 blockID);

private:
	U64            samples;    // has to match header
	float          version;    // has to match header
	U64            filesize_;  // filesize
	U64            offset_end_of_data_;
	BYTE           bit_width_; // bit width
	bool           dropGenotypes;
	bool           showHeader; // flag to output header or not
	std::ifstream  stream_;    // reader stream

	header_type header_;
	footer_type footer_;
	index_type* index_;


	buffer_type          buffer_;       // input buffer
	buffer_type          data_;         // inflate buffer
	buffer_type          outputBuffer_; // output buffer
	tgzf_controller_type tgzf_controller_;

	std::vector<DataOffsetPair> blockDataOffsets_;

	IO::GenericWriterInterace* writer;
};

template <class T>
bool TomahawkReader::outputBlock(const U32 blockID){
	if(!this->stream_.good()){
		std::cerr << Helpers::timestamp("ERROR", "TWK") << "Stream bad " << blockID << std::endl;
		return false;
	}

	//std::cerr << "getblock " << blockID  << " seek to " << this->index_->getContainer()[blockID].byte_offset << std::endl;
	this->stream_.seekg(this->index_->getContainer()[blockID].byte_offset);
	if(!this->stream_.good()){
		std::cerr << Helpers::timestamp("ERROR", "TWK") << "Failed search..." << std::endl;
		return false;
	}

	// Determine byte-width of data
	U32 readLength = 0;
	if(blockID != this->index_->getContainer().size() - 1)
		readLength = this->index_->getContainer()[blockID + 1].byte_offset - this->index_->getContainer()[blockID].byte_offset;
	else
		readLength = this->filesize_ - Constants::eof_length*sizeof(U64) - this->index_->getContainer()[this->index_->getContainer().size()-1].byte_offset;

	if(readLength > this->buffer_.capacity()){
		std::cerr << Helpers::timestamp("ERROR", "TWK") << "Impossible: " << readLength << '/' << this->buffer_.capacity() << std::endl;
		exit(1);
	}

	// Read from start to start + byte-width
	if(!this->stream_.read(this->buffer_.data(), readLength)){
		std::cerr << Helpers::timestamp("ERROR", "TWK") << "Failed read: " << this->stream_.good() << '\t' << this->stream_.fail() << '/' << this->stream_.eof() << std::endl;
		std::cerr << this->stream_.gcount() << '/' << readLength << std::endl;
		return false;
	}
	// Set buffer byte-width to data loaded
	this->buffer_.n_chars = readLength;

	// Keep track of position because inflate function moves pointer
	char* data_position = &this->data_[this->data_.n_chars];

	// Inflate TGZF block
	if(!this->tgzf_controller_.Inflate(this->buffer_, this->data_)){
		std::cerr << Helpers::timestamp("ERROR", "TGZF") << "Failed to inflate DATA..." << std::endl;
		return false;
	}

	this->WriteBlock<T>(data_position, blockID);

	return true;
}

template <class T>
bool TomahawkReader::WriteBlock(const char* const data, const U32 blockID){
	Base::GenotypeContainerReference<T> o(data, this->index_->getContainer()[blockID].uncompressed_size, this->index_->getContainer()[blockID], this->samples);

	// For each variant in Tomahawk block
	for(U32 j = 0; j < o.size(); ++j){
		const char separator = o.currentMeta().getPhaseVCFCharacter();

		this->outputBuffer_ += this->header_.contigs_[this->index_->getContainer()[blockID].contigID].name;
		this->outputBuffer_ += '\t';
		this->outputBuffer_ += std::to_string(o.currentMeta().position);
		this->outputBuffer_ += "\t.\t";
		this->outputBuffer_ += o.currentMeta().getRefAllele();
		this->outputBuffer_ += '\t';
		this->outputBuffer_ += o.currentMeta().getAltAllele();
		this->outputBuffer_ += "\t.\t.\t";
		this->outputBuffer_ += "HWE_P=";
		this->outputBuffer_ += std::to_string(o.currentMeta().HWE_P);
		this->outputBuffer_ += ";MAF=";
		this->outputBuffer_ += std::to_string(o.currentMeta().MAF);
		if(this->dropGenotypes == false){
			this->outputBuffer_ += "\tGT\t";
			for(U32 i = 0; i < o.currentMeta().runs; ++i){
				if(i != 0) this->outputBuffer_ += '\t';
				const char& left  = Constants::TOMAHAWK_ALLELE_LOOKUP_REVERSE[o[i].alleleA];
				const char& right = Constants::TOMAHAWK_ALLELE_LOOKUP_REVERSE[o[i].alleleB];
				this->outputBuffer_ += left;
				this->outputBuffer_ += separator;
				this->outputBuffer_ += right;
				for(U32 r = 1; r < o[i].runs; ++r){
					this->outputBuffer_ += '\t';
					this->outputBuffer_ += left;
					this->outputBuffer_ += separator;
					this->outputBuffer_ += right;
				}
			}
		}
		this->outputBuffer_ += '\n';
		++o;

		if(this->outputBuffer_.size() > 65536){
			//this->writer->write(this->outputBuffer_.data(), this->outputBuffer_.n_chars);
			std::cout.write(this->outputBuffer_.data(), this->outputBuffer_.n_chars);
			this->outputBuffer_.reset();
		}
	}

	// Flush last
	//this->writer->write(this->outputBuffer_.data(), this->outputBuffer_.n_chars);
	std::cout.write(this->outputBuffer_.data(), this->outputBuffer_.n_chars);

	// Reset buffers
	this->outputBuffer_.reset(); // reset
	this->data_.reset(); // reset

	return true;
}

} /* namespace Tomahawk */

#endif /* TOMAHAWK_TOMAHAWKREADER_H_ */
