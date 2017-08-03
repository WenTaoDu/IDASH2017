#include "TestAK.h"

#include <NTL/BasicThreadPool.h>
#include <NTL/ZZ.h>
#include <Params.h>
#include <PubKey.h>
#include <Scheme.h>
#include <SchemeAlgo.h>
#include <SchemeAux.h>
#include <SecKey.h>
#include <TimeUtils.h>
#include <NumUtils.h>
#include <cmath>
#include <iostream>

#include "CipherGD.h"
#include "GD.h"

using namespace NTL;

//-----------------------------------------

void TestAK::testNLGDWB() {
	cout << "!!! START TEST NLGD WB !!!" << endl;
	//-----------------------------------------
	TimeUtils timeutils;
	SetNumThreads(8);
	//-----------------------------------------
	GD sgd;

//	string filename = "data/data5x500.txt";     // false   415/500 done
//	string filename = "data/data9x1253.txt";    // false   775/1253 unclear
//	string filename = "data/data15x1500.txt";   // false   1270/1500 done
//	string filename = "data/data16x101.txt";    // false   101/101 done
//	string filename = "data/data27x148.txt";    // false   132/148 done
//	string filename = "data/data43x3247.txt";   // false   3182/3247
//	string filename = "data/data45x296.txt";    // false   257/296
//	string filename = "data/data51x653.txt";    // false   587/653
//	string filename = "data/data67x216.txt";    // false   216/216
//	string filename = "data/data103x1579.txt";  // true    1086/1579

	long factorDim = 0;
	long sampleDim = 0;

	long** xyData = sgd.xyDataFromFile(filename, factorDim, sampleDim, false);

	long sdimBits = (long)ceil(log2(sampleDim));
	long sampleDimPo2 = (1 << sdimBits);
	long fdimBits = (long)ceil(log2(factorDim));
	long factorDimPo2 = (1 << fdimBits);
	long learnDim = sampleDim;
	long ldimBits = (long)ceil(log2(learnDim));
	long learnDimPo2 = (1 << ldimBits);

	cout << "factorDim: " << factorDim << endl;
	cout << "fdimBits: " << fdimBits << endl;
	cout << "factorDimPo2: " << factorDimPo2 << endl;
	cout << "sampleDim: " << sampleDim << endl;
	cout << "sdimBits: " << sdimBits << endl;
	cout << "sampleDimPo2: " << sampleDimPo2 << endl;
	cout << "learnDim: " << learnDim << endl;
	cout << "ldimBits: " << ldimBits << endl;
	cout << "learnDimPo2: " << learnDimPo2 << endl;

	long wBatch = 1;
	long iter = fdimBits;
//	long iter = 5000;
	long logl = 5;
	long logp = 32;
	long L = 5 * iter + 1;
	long logN = Params::suggestlogN(80, logl, logp, L);
//	long logN = max(12, ldimBits);
	bool encrypted = false;
	long slots =  learnDimPo2 * wBatch;

	cout << "logl: " << logl << endl;
	cout << "logp: " << logp << endl;
	cout << "L: " << L << endl;
	cout << "logN: " << logN << endl;
	cout << "slots: " << slots << endl;
	cout << "wBatch: " << wBatch << endl;

	double** vData = new double*[wBatch];
	double** wData = new double*[wBatch];
	for (long l = 0; l < wBatch; ++l) {
		wData[l] = new double[factorDim];
		vData[l] = new double[factorDim];
		for (long i = 0; i < factorDim; ++i) {
			double tmp = (0.5 - 1.0 * (double)rand() / RAND_MAX) / factorDim;
//			double tmp = 0;
			wData[l][i] = tmp;
			vData[l][i] = tmp;
		}
	}

	double* alpha = new double[iter + 2]; // just constansts for Nesterov GD
	alpha[0] = 0.1;
	for (long i = 1; i < iter + 2; ++i) {
		alpha[i] = (1. + sqrt(1. + 4.0 * alpha[i-1] * alpha[i-1])) / 2.0;
	}

	if(!encrypted) {
		timeutils.start("sgd");
		for (long k = 0; k < iter; ++k) {

			double gamma = 2.0 / learnDim / (1.0 + k);
			double eta = (1. - alpha[k+1]) / alpha[k+2];

			for (long l = 0; l < wBatch; ++l) {
				sgd.stepNLGD(xyData, wData[l], vData[l], factorDim, learnDim, gamma, eta);
			}
		}
		timeutils.stop("sgd");
		double* w = sgd.wsum(wData, factorDim, wBatch);
		sgd.check(xyData, w, factorDim, sampleDim);
	} else {
		//-----------------------------------------
		Params params(logN, logl, logp, L);
		SecKey secretKey(params);
		PubKey publicKey(params, secretKey);
		SchemeAux schemeaux(params);
		Scheme scheme(params, publicKey, schemeaux);
		SchemeAlgo algo(scheme);
		CipherGD csgd(scheme, algo, secretKey);
		//-----------------------------------------

		timeutils.start("Enc xyData");
		Cipher* cxyData = csgd.encxyDataWB(xyData, slots, factorDim, learnDim, wBatch);
		timeutils.stop("Enc xyData");

		timeutils.start("Enc wData");
		Cipher* cwData = csgd.encwDataWB(wData, slots, factorDim, learnDim, wBatch);
		timeutils.stop("Enc wData");

		Cipher* cvData = new Cipher[factorDim];
		for (long i = 0; i < factorDim; ++i) {cvData[i] = cwData[i];}

		for (long k = 0; k < iter; ++k) {
			double gamma = 2.0 / learnDim / (1.0 + k);
			double eta = (1. - alpha[k+1]) / alpha[k+2];

			timeutils.start("Enc sgd step");
			csgd.encStepNLGDWB(cxyData, cwData, cvData, slots, factorDim, learnDim, wBatch, gamma, eta);
			timeutils.stop("Enc sgd step");
			csgd.debugcheck("c wData: ", secretKey, cwData, 5, wBatch);
		}

		timeutils.start("Enc w out");
		csgd.encwsumWB(cwData, factorDim, wBatch);
		timeutils.stop("Enc w out");

		timeutils.start("Dec w");
		double* dw = csgd.decWB(secretKey, cwData, factorDim);
		timeutils.stop("Dec w");

		sgd.check(xyData, dw, factorDim, sampleDim);
	}
	//-----------------------------------------
	cout << "!!! END TEST NLGD WB !!!" << endl;
}

void TestAK::testNLGDXYB() {
	cout << "!!! START TEST NLGD XYB !!!" << endl;
	//-----------------------------------------
	TimeUtils timeutils;
	SetNumThreads(8);
	//-----------------------------------------
	GD sgd;

//	string filename = "data/data5x500.txt";     // false   415/500
//	string filename = "data/data9x1253.txt";    // false   775/1253
//	string filename = "data/data15x1500.txt";   // false   1270/1500
//	string filename = "data/data16x101.txt";    // false   101/101
//	string filename = "data/data27x148.txt";    // false   132/148
//	string filename = "data/data43x3247.txt";   // false   3182/3247
//	string filename = "data/data45x296.txt";    // false   257/296
//	string filename = "data/data51x653.txt";    // false   587/653
//	string filename = "data/data67x216.txt";    // false   216/216
	string filename = "data/data103x1579.txt";  // true    1086/1579

	long factorDim = 0;
	long sampleDim = 0;

	long** xyData = sgd.xyDataFromFile(filename, factorDim, sampleDim, false);

	long sdimBits = (long)ceil(log2(sampleDim));
	long sampleDimPo2 = (1 << sdimBits);
	long fdimBits = (long)ceil(log2(factorDim));
	long factorDimPo2 = (1 << fdimBits);
	long learnDim = (1 << (sdimBits - 1));
	long ldimBits = (long)ceil(log2(learnDim));
	long learnDimPo2 = (1 << ldimBits);

	cout << "factorDim: " << factorDim << endl;
	cout << "fdimBits: " << fdimBits << endl;
	cout << "factorDimPo2: " << factorDimPo2 << endl;
	cout << "sampleDim: " << sampleDim << endl;
	cout << "sdimBits: " << sdimBits << endl;
	cout << "sampleDimPo2: " << sampleDimPo2 << endl;
	cout << "learnDim: " << learnDim << endl;
	cout << "ldimBits: " << ldimBits << endl;
	cout << "learnDimPo2: " << learnDimPo2 << endl;

	bool encrypted = true;
	long iter = fdimBits;
	long logl = 5;
	long logp = 32;
	long L = 6 * iter + 1;
	long logN = Params::suggestlogN(80, logl, logp, L);
//	long logN = max(12, ldimBits);

	long xybatchBits = 6; // logN - 1 - ldimBits
	long xyBatch = (1 << xybatchBits);
	long slots =  learnDimPo2 * xyBatch;
	long cnum = factorDimPo2 / xyBatch;

	cout << "logl: " << logl << endl;
	cout << "logp: " << logp << endl;
	cout << "L: " << L << endl;
	cout << "logN: " << logN << endl;
	cout << "slots: " << slots << endl;
	cout << "xyBatch: " << xyBatch << endl;
	cout << "cnum: " << cnum << endl;


	double* vData = new double[factorDim];
	double* wData = new double[factorDim];
	for (long i = 0; i < factorDim; ++i) {
//		double tmp = (0.5 - 1.0 * (double)rand() / RAND_MAX) / factorDim;
		double tmp = 0.0;
		wData[i] = tmp;
		vData[i] = tmp;
	}

	double* alpha = new double[iter + 2]; // just constansts for Nesterov GD
	alpha[0] = 0.1;
	for (long i = 1; i < iter + 2; ++i) {
		alpha[i] = (1. + sqrt(1. + 4.0 * alpha[i-1] * alpha[i-1])) / 2.0;
	}

	if(!encrypted) {
		timeutils.start("sgd");
		for (long k = 0; k < iter; ++k) {

			double gamma = 1.0 / learnDim / (2 << k);
			double eta = (1. - alpha[k+1]) / alpha[k+2];

			sgd.stepNLGD(xyData, wData, vData, factorDim, learnDim, gamma, eta);
		}
		timeutils.stop("sgd");
		sgd.check(xyData, wData, factorDim, sampleDim);
	} else {
		//-----------------------------------------
		Params params(logN, logl, logp, L);
		SecKey secretKey(params);
		PubKey publicKey(params, secretKey);
		SchemeAux schemeaux(params);
		Scheme scheme(params, publicKey, schemeaux);
		SchemeAlgo algo(scheme);
		CipherGD csgd(scheme, algo, secretKey);
		//-----------------------------------------

		CZZ* pvals = new CZZ[slots];
		for (long j = 0; j < learnDim; ++j) {
			pvals[xyBatch * j] = CZZ(params.p);
		}

		CZZ* pdvals = scheme.groupidx(pvals, slots);
		Message msg = scheme.encode(pdvals, slots);

		timeutils.start("Enc xyData XYB");
		Cipher* cxyData = csgd.encxyDataXYB(xyData, slots, factorDim, learnDim, learnDimPo2, xyBatch, cnum);
		timeutils.stop("Enc xyData");

		timeutils.start("Enc wData XYB");
		Cipher* cwData = csgd.encwDataXYB(wData, slots, factorDim, learnDim, learnDimPo2, xyBatch, cnum);
		timeutils.stop("Enc wData");

		Cipher* cvData = new Cipher[cnum];
		for (long i = 0; i < cnum; ++i) {cvData[i] = cwData[i];}

		for (long k = 0; k < iter; ++k) {
			double gamma = 1.0 / learnDim / (2 << k);
			double eta = (1. - alpha[k+1]) / alpha[k+2];

			timeutils.start("Enc nlgd step");
			csgd.encStepNLGDXYB(cxyData, cwData, cvData, msg.mx, slots, learnDim, learnDimPo2, xybatchBits, xyBatch, cnum, gamma, eta);
			timeutils.stop("Enc nlgd step");
			double* dw = csgd.decXYB(secretKey,cwData, factorDim, xyBatch, cnum);
			sgd.check(xyData, dw, factorDim, sampleDim);
		}

		timeutils.start("Dec w");
		double* dw = csgd.decXYB(secretKey,cwData, factorDim, xyBatch, cnum);
		timeutils.stop("Dec w");

		sgd.check(xyData, dw, factorDim, sampleDim);
	}
	//-----------------------------------------
	cout << "!!! END TEST NLGD XYB !!!" << endl;
}
