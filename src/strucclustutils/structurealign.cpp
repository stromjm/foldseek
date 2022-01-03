#include "DBReader.h"
#include "IndexReader.h"
#include "DBWriter.h"
#include "Debug.h"
#include "Util.h"
#include "LocalParameters.h"
#include "Matcher.h"
#include "Alignment.h"
#include "structureto3diseqdist.h"
#include "StructureSmithWaterman.h"
#include "StructureUtil.h"

#ifdef OPENMP
#include <omp.h>
#endif

int structurealign(int argc, const char **argv, const Command& command) {
    LocalParameters &par = LocalParameters::getLocalInstance();
    par.parseParameters(argc, argv, command, true, 0, MMseqsParameter::COMMAND_ALIGN);

    const bool touch = (par.preloadMode != Parameters::PRELOAD_MODE_MMAP);
    IndexReader qdbrAA(par.db1, par.threads, IndexReader::SEQUENCES, touch ? IndexReader::PRELOAD_INDEX : 0);
    IndexReader qdbr(StructureUtil::getIndexWithSuffix(par.db1, "_ss"), par.threads, IndexReader::SEQUENCES, touch ? IndexReader::PRELOAD_INDEX : 0);

    IndexReader *t3DiDbr = NULL;
    IndexReader *tAADbr = NULL;
    bool sameDB = false;
    if (par.db1.compare(par.db2) == 0) {
        sameDB = true;
        t3DiDbr = &qdbr;
        tAADbr = &qdbrAA;
    } else {
        tAADbr = new IndexReader(par.db2, par.threads, IndexReader::SEQUENCES, touch ? IndexReader::PRELOAD_INDEX : 0);
        t3DiDbr = new IndexReader(StructureUtil::getIndexWithSuffix(par.db2, "_ss"), par.threads, IndexReader::SEQUENCES, touch ? IndexReader::PRELOAD_INDEX : 0);
    }

    DBReader<unsigned int> resultReader(par.db3.c_str(), par.db3Index.c_str(), par.threads, DBReader<unsigned int>::USE_DATA|DBReader<unsigned int>::USE_INDEX);
    resultReader.open(DBReader<unsigned int>::LINEAR_ACCCESS);

    DBWriter dbw(par.db4.c_str(), par.db4Index.c_str(), static_cast<unsigned int>(par.threads), par.compressed,  Parameters::DBTYPE_ALIGNMENT_RES);
    dbw.open();

    SubstitutionMatrix subMat3Di(par.scoringMatrixFile.values.aminoacid().c_str(), 2.1, par.scoreBias);
    std::string blosum;
    for (size_t i = 0; i < par.substitutionMatrices.size(); i++) {
        if (par.substitutionMatrices[i].name == "blosum62.out") {
            std::string matrixData((const char *)par.substitutionMatrices[i].subMatData, par.substitutionMatrices[i].subMatDataLen);
            std::string matrixName = par.substitutionMatrices[i].name;
            char * serializedMatrix = BaseMatrix::serialize(matrixName, matrixData);
            blosum.assign(serializedMatrix);
            free(serializedMatrix);
            break;
        }
    }
    SubstitutionMatrix subMatAA(blosum.c_str(), 1.4, par.scoreBias);

    //temporary output file
    Debug::Progress progress(resultReader.getSize());

    // sub. mat needed for query profile
    int8_t * tinySubMatAA = (int8_t*) mem_align(ALIGN_INT, subMatAA.alphabetSize * 32);
    int8_t * tinySubMat3Di = (int8_t*) mem_align(ALIGN_INT, subMat3Di.alphabetSize * 32);

    for (int i = 0; i < subMat3Di.alphabetSize; i++) {
        for (int j = 0; j < subMat3Di.alphabetSize; j++) {
            tinySubMat3Di[i * subMat3Di.alphabetSize + j] = subMat3Di.subMatrix[i][j]; // for farrar profile
        }
    }
    for (int i = 0; i < subMatAA.alphabetSize; i++) {
        for (int j = 0; j < subMatAA.alphabetSize; j++) {
            tinySubMatAA[i * subMatAA.alphabetSize + j] = subMatAA.subMatrix[i][j];
        }
    }

    EvalueComputation evaluer(tAADbr->sequenceReader->getAminoAcidDBSize(), &subMat3Di, par.gapOpen.values.aminoacid(), par.gapExtend.values.aminoacid());
#pragma omp parallel
    {
        unsigned int thread_idx = 0;
#ifdef OPENMP
        thread_idx = static_cast<unsigned int>(omp_get_thread_num());
#endif

        std::vector<Matcher::result_t> alignmentResult;
        StructureSmithWaterman structureSmithWaterman(par.maxSeqLen, subMat3Di.alphabetSize, false);
        StructureSmithWaterman reverseStructureSmithWaterman(par.maxSeqLen, subMat3Di.alphabetSize, false);

        Sequence qSeqAA(par.maxSeqLen, Parameters::DBTYPE_AMINO_ACIDS, (const BaseMatrix *) &subMatAA, 0, false, par.compBiasCorrection);
        Sequence qSeq3Di(par.maxSeqLen, Parameters::DBTYPE_AMINO_ACIDS, (const BaseMatrix *) &subMat3Di, 0, false, par.compBiasCorrection);
        Sequence tSeqAA(par.maxSeqLen, Parameters::DBTYPE_AMINO_ACIDS, (const BaseMatrix *) &subMatAA, 0, false, par.compBiasCorrection);
        Sequence tSeq3Di(par.maxSeqLen, Parameters::DBTYPE_AMINO_ACIDS, (const BaseMatrix *) &subMat3Di, 0, false, par.compBiasCorrection);
        std::string backtrace;
        char buffer[1024+32768];
        std::string resultBuffer;

        // write output file

#pragma omp for schedule(dynamic, 1)
        for (size_t id = 0; id < resultReader.getSize(); id++) {
            progress.updateProgress();
            char *data = resultReader.getData(id, thread_idx);
            size_t queryKey = resultReader.getDbKey(id);
            if(*data != '\0') {
                unsigned int queryId = qdbr.sequenceReader->getId(queryKey);

                char *querySeqAA = qdbrAA.sequenceReader->getData(queryId, thread_idx);
                char *querySeq3Di = qdbr.sequenceReader->getData(queryId, thread_idx);

                unsigned int querySeqLen = qdbr.sequenceReader->getSeqLen(queryId);
                qSeq3Di.mapSequence(id, queryKey, querySeq3Di, querySeqLen);
                qSeqAA.mapSequence(id, queryKey, querySeqAA, querySeqLen);
                int32_t maskLen = querySeqLen / 2;

                structureSmithWaterman.ssw_init(&qSeqAA, &qSeq3Di, tinySubMatAA, tinySubMat3Di, &subMatAA);
                qSeq3Di.reverse();
                qSeqAA.reverse();
                reverseStructureSmithWaterman.ssw_init(&qSeqAA, &qSeq3Di, tinySubMatAA, tinySubMat3Di, &subMatAA);
                int passedNum = 0;
                int rejected = 0;
                while (*data != '\0' && passedNum < par.maxAccept && rejected < par.maxRejected) {
                    char dbKeyBuffer[255 + 1];
                    Util::parseKey(data, dbKeyBuffer);
                    data = Util::skipLine(data);
                    const unsigned int dbKey = (unsigned int) strtoul(dbKeyBuffer, NULL, 10);
                    unsigned int targetId = t3DiDbr->sequenceReader->getId(dbKey);
                    const bool isIdentity = (queryId == targetId && (par.includeIdentity || sameDB))? true : false;

                    char * targetSeq3Di = t3DiDbr->sequenceReader->getData(targetId, thread_idx);
                    char * targetSeqAA = tAADbr->sequenceReader->getData(targetId, thread_idx);
                    const int targetLen = static_cast<int>(t3DiDbr->sequenceReader->getSeqLen(targetId));

                    tSeq3Di.mapSequence(targetId, dbKey, targetSeq3Di, targetLen);
                    tSeqAA.mapSequence(targetId, dbKey, targetSeqAA, targetLen);
                    if(Util::canBeCovered(par.covThr, par.covMode, qSeq3Di.L, targetLen) == false){
                        continue;
                    }



                    backtrace.clear();
                    StructureSmithWaterman::s_align align = structureSmithWaterman.ssw_align(tSeqAA.numSequence, tSeq3Di.numSequence, targetLen, par.gapOpen.values.aminoacid(),
                                                     par.gapExtend.values.aminoacid(), par.alignmentMode, backtrace,
                                                     par.evalThr, &evaluer, par.covMode, par.covThr, maskLen);
                    StructureSmithWaterman::s_align revAlign = reverseStructureSmithWaterman.ssw_align(tSeqAA.numSequence, tSeq3Di.numSequence, targetLen, par.gapOpen.values.aminoacid(),
                                                                                             par.gapExtend.values.aminoacid(), 0, backtrace,
                                                                                             par.evalThr, &evaluer, par.covMode, par.covThr, maskLen);

                    align.score1 = std::max(1, static_cast<int32_t>(align.score1) - static_cast<int32_t>(revAlign.score1));
                    align.evalue = evaluer.computeEvalue(align.score1, querySeqLen);
                    unsigned int alnLength = Matcher::computeAlnLength(align.qStartPos1, align.qEndPos1, align.dbStartPos1, align.dbEndPos1);
                    if(backtrace.size() > 0){
                        alnLength = backtrace.size();
                    }
                    float seqId = Util::computeSeqId(par.seqIdMode, align.identicalAACnt, querySeqLen, targetLen, alnLength);

                    Matcher::result_t res(dbKey, align.score1, align.qCov, align.tCov, seqId, align.evalue, alnLength, align.qStartPos1, align.qEndPos1, querySeqLen, align.dbStartPos1, align.dbEndPos1, targetLen, backtrace);

                    //Matcher::result_t res = paruenAlign.align(qSeq3Di, tSeq3Di, &subMat3Di, evaluer);

                    if (Alignment::checkCriteria(res, isIdentity, par.evalThr, par.seqIdThr, par.alnLenThr, par.covMode, par.covThr)) {
                        alignmentResult.emplace_back(res);
                        passedNum++;
                        rejected = 0;
                    }

                    else {
                        rejected++;
                    }
                }
            }
            if (alignmentResult.size() > 1) {
                SORT_SERIAL(alignmentResult.begin(), alignmentResult.end(), Matcher::compareHits);
            }
            for (size_t result = 0; result < alignmentResult.size(); result++) {
                size_t len = Matcher::resultToBuffer(buffer, alignmentResult[result], par.addBacktrace);
                resultBuffer.append(buffer, len);
            }
            dbw.writeData(resultBuffer.c_str(), resultBuffer.length(), queryKey, thread_idx);
            resultBuffer.clear();
            alignmentResult.clear();
        }
    }

    free(tinySubMatAA);
    free(tinySubMat3Di);

    dbw.close();
    resultReader.close();
    if (sameDB == false) {
        delete t3DiDbr;
        delete tAADbr;
    }
    return EXIT_SUCCESS;
}