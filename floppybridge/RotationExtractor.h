#ifndef READERWRITER_ROTATION_EXTRACTOR
#define READERWRITER_ROTATION_EXTRACTOR
/* ArduinoFloppyReader (and writer) - Rotation Extractor
*
* Copyright (C) 2017-2021 Robert Smith (@RobSmithDev)
* https://amiga.robsmithdev.co.uk
*
* This library is free software; you can redistribute it and/or
* modify it under the terms of the GNU Library General Public
* License as published by the Free Software Foundation; either
* version 3 of the License, or (at your option) any later version.
*
* This library is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
* Library General Public License for more details.
*
* You should have received a copy of the GNU Library General Public
* License along with this library; if not, see http://www.gnu.org/licenses/
*/

////////////////////////////////////////////////////////////////////////////////////////
// Class to manage finding *exact* disk revolutions                                   //
////////////////////////////////////////////////////////////////////////////////////////
//
// Purpose:
// The class attempts to guess where an exact disk revolution occurs, and then re-aligns
// it such that it starts at the index pulse.  This means we don't need to wait for an index
// pulse to work out a revolution of the disk.  The first time a disk is used we calculate
// the time of a single revolution and then use that as a guide to how long a revolution will
// take in the future.
//
// This isn't 100% perfect but does seem to work.

// With this defined you get speed data per bit.  Undefined, per 8 bits.  Nothing seems to notice much
// Inside *UAE it uses one speed value for 16 bits of MFM data so that's probably why.
// and besides, with this *undefined* we save about 700k of ram
//#define HIGH_RESOLUTION_MODE

// Instead of outputting "speed" values this will output bit-times in ns
//#define OUTPUT_TIME_IN_NS

// So worse case is the disk takes 210ms to spin, and every sequence is VERY fast, and every sequence is 01, this number is highly unlikely though
// The x2 is for HD disks
#define MAX_REVOLUTION_SEQUENCES		(120000 * 2)

// Number of sequences to match to find the index overlap position or the rotation overlap position
// The higher the number, the more chance of perfect revolution alignment, but higher processing required at the end of each revolution
#define OVERLAP_SEQUENCE_MATCHES		1024

// Extra window either side.  this allows more of a search range
#define OVERLAP_EXTRA_BUFFER			6

// Signal for index was not found
#define INDEX_NOT_FOUND					0xFFFFFFFF


// Class to extract a single rotation from an incoming mfm data sequence.
class RotationExtractor {
public:

	// Enum for the possible sequences we support
	enum class MFMSequence : unsigned char { mfm01 = 0, mfm001 = 1, mfm0001 = 2, mfm0000 = 3 };

	// A single sequence of MFM data
	struct MFMSequenceInfo {
		// Total time it took to read this in NS
		unsigned short timeNS;

		// The MFM sequence discovered
		MFMSequence mfm;
	};

	// Decoded version of the above
	struct MFMSample {
#ifdef OUTPUT_TIME_IN_NS
		// This is the time for each 'bit' 
		unsigned short bittime[8];
#else
#ifdef HIGH_RESOLUTION_MODE
		// This is the speed of each 'bit' as a %
		unsigned short speed[8];
#else
		// This is the average speed of all 8 bits as a %
		unsigned short speed;
#endif
#endif

		// This is the raw MFM bit-data
		unsigned char mfmData;
	};

	// Struct for tracking what the index start looks like so we get it perfect (or at least consistent)
	struct IndexSequenceMarker {
		// Sequences found
		MFMSequence sequences[OVERLAP_SEQUENCE_MATCHES];

		// If this is actually valid
		bool valid = false;
	};

private:
	// How long a revolution is
	unsigned int m_revolutionTime = 0;
	// An amount of time whereby we 'nearly' have a complete revolution of data
	unsigned int m_revolutionTimeNearlyComplete = 0;
	// Used while working out the above
	unsigned int m_revolutionTimeCounting = 0;
	// Where the first index pulse was discovered
	unsigned int m_sequenceIndex = INDEX_NOT_FOUND;
	// Where the second index pulse was discovered
	unsigned int m_nextSequenceIndex = INDEX_NOT_FOUND;
	// Where we were when we reached m_revolutionTime worth of data
	unsigned int m_revolutionReadyAt = INDEX_NOT_FOUND;
	// And a flag to set this as good
	bool m_revolutionReady = false;
	// Is simple mode enabled?
	bool m_useSimpleMode = false;
	// Is this an HD disk?
	bool m_isHD = false;
	// If we should always use the index marker when finding revolutions
	bool m_useIndex = false;
	// Current amount of data in the buffer in ns
	unsigned int m_currentTime = 0;
	// Current position of the buffer
	unsigned int m_sequencePos = 0;
	// Used to track exactly how much data has been submitted
	unsigned int m_timeReceived = 0;
	// Sequences received thus far
	MFMSequenceInfo* m_sequences; // [MAX_REVOLUTION_SEQUENCES] ;
	// In index mode, this holds the initial sequences before the first index marker
	MFMSequenceInfo* m_initialSequences; // [OVERLAP_SEQUENCE_MATCHES * OVERLAP_EXTRA_BUFFER] ;
	// Length of the above datat in use
	unsigned int m_initialSequencesLength = 0;
	// Where we're writing to as its a circular buffer
	unsigned int m_initialSequencesWritePos = 0;
	// Sequences discovered around the index marker
	IndexSequenceMarker m_indexSequence;

	// Finds the overlap between the start of the data and where we currently are
	unsigned int getOverlapPosition() const;

	// is almost identical
	const unsigned int getTrueIndexPosition(const unsigned int revolutionEnd, const unsigned int startingPoint = INDEX_NOT_FOUND);
public:
	RotationExtractor();
	virtual ~RotationExtractor();

	// Get and set the sequence identified as data round the INDEX pulse so that next time we get consistent revolution starting points
	void setIndexSequence(const IndexSequenceMarker& sequence) { m_indexSequence = sequence; }
	void getIndexSequence(IndexSequenceMarker& sequence) const { sequence = m_indexSequence; }

	// Reset this back to "empty"
	inline void reset(bool isHD) {
		m_indexSequence.valid = false;
		m_revolutionReadyAt = INDEX_NOT_FOUND;
		m_sequencePos = 0;
		m_sequenceIndex = INDEX_NOT_FOUND;
		m_nextSequenceIndex = INDEX_NOT_FOUND;
		m_currentTime = 0;
		m_revolutionReady = false;
		m_initialSequencesLength = 0;
		m_initialSequencesWritePos = 0;
		m_timeReceived = 0;
		m_isHD = isHD;
	}

	// Signal new disk, or maybe a motor restarted.  Need to re-calculate rotation speed.  In HD mode, the data must be fed in at DD speeds (4, 6 and 8us)
	inline void newDisk(bool isHD) {
		reset(isHD);
		m_revolutionTime = 0;
		m_revolutionTimeCounting = 0;
		m_revolutionTimeNearlyComplete = 0;
	}

	// Return the current revolution time
	inline unsigned int getRevolutionTime() const { return m_revolutionTime; };

	// Set the current revolution time
	inline void setRevolutionTime(const unsigned int time) { m_revolutionTime = time; m_revolutionTimeNearlyComplete = (unsigned int)(time * 0.9f); };

	// Return the total amount of time data received so far
	inline unsigned int totalTimeReceived() const { return m_timeReceived; };

	// Returns TRUE if this has learnt the time of a disk revolution
	inline bool hasLearntRotationSpeed() const { return m_revolutionTime > (m_isHD ? 300000000U : 150000000U); };

	// Returns TRUE if we're in INDEX mode
	inline bool isInIndexMode() const { return m_useIndex; };

	// Sets the code so it always uses the index marker when finding revolutions
	void setAlwaysUseIndex(bool useIndex) { m_useIndex = useIndex; };

	// In simple mode, the rotation is matched purely on Index pulses alone, the data is not matched. Index mode must be enabled. This is fine for SCP reading etc
	void setSimpleMode(bool simpleMode) { m_useSimpleMode = simpleMode; };

	// If this is about to spit out a revolution in a very small amount of time
	inline bool isNearlyReady() const { return (m_revolutionTimeNearlyComplete) && (m_currentTime >= m_revolutionTimeNearlyComplete) && (!m_useIndex); }

	// Submit a single sequence to the list
	void submitSequence(const MFMSequenceInfo& sequence, const bool isIndex, const bool discardEarlySamples = true);

	// Returns TRUE if we should be able to extract a revolution
	inline bool canExtract() const { return (m_revolutionReadyAt != INDEX_NOT_FOUND) && (m_revolutionReady); };

	// Extracts a single rotation and updates the buffer to remove it.  Returns FALSE if no rotation is available
	// If calculateSpeedFactor is true, we're in INDEX mode, and HIGH_RESOLUTION_MODE is defined then this will output time in NS rather than the speed factor value
	bool extractRotation(MFMSample* output, unsigned int& outputBits, const unsigned int maxBufferSizeBytes);
};


#endif