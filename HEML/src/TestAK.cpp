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

void TestAK::testAK(long logN, long logl, long logp, long L) {
	cout << "!!! START TEST SGD !!!" << endl;
	//-----------------------------------------
	TimeUtils timeutils;
	SetNumThreads(8);
	//-----------------------------------------
	GD sgd;

	string filename = "data103x1579.txt";
//	string filename = "data15x1500.txt";
//	string filename = "data5x500.txt";
//	string filename = "data9x1253.txt";

	long factorDim = 0;
	long sampleDim = 0;

	long** xyData = sgd.xyDataFromFile(filename, factorDim, sampleDim); // factorDim=103, sampleDim=1579

	long sdimBits = (long)ceil(log2(sampleDim)); // 11
	long sampleDimPo2 = (1 << sdimBits); // 2048

	long fdimBits = (long)ceil(log2(factorDim)); // 7
	long factorDimPo2 = (1 << fdimBits); // 128

	long learnDim = sampleDim; // 1579
	long ldimBits = (long)ceil(log2(learnDim)); //11
	long learnDimPo2 = (1 << ldimBits); // 2048

	long slots =  (1 << (logN-1)); // 2^11 = 2048
	long wBatch = slots / learnDimPo2; // 1

	cout << "factorDim: " << factorDim << endl;
	cout << "factorDimPo2: " << factorDimPo2 << endl;

	cout << "sampleDim: " << sampleDim << endl;
	cout << "sampleDimPo2: " << sampleDimPo2 << endl;

	cout << "learnDim: " << learnDim << endl;
	cout << "learnDimPo2: " << learnDimPo2 << endl;

	cout << "slots: " << slots << endl;
	cout << "wBatch: " << wBatch << endl;

	double** vData = new double*[wBatch]; // wBatch x factorDim
	double** wData = new double*[wBatch]; // wBatch x factorDim
	for (long l = 0; l < wBatch; ++l) {
		wData[l] = new double[factorDim];
		vData[l] = new double[factorDim];
		for (long i = 0; i < factorDim; ++i) {
			double tmp = 0.0;
			wData[l][i] = tmp;
			vData[l][i] = tmp;
		}
	}

	long iter = 0;
	long enciteradded = 10;
	long totaliter = iter + enciteradded;

	double* alpha = new double[totaliter + 2]; // just constansts for Nesterov GD
	alpha[0] = 0.0;
	for (long i = 1; i < totaliter + 2; ++i) {
		alpha[i] = (1. + sqrt(1. + 4.0 * alpha[i-1] * alpha[i-1])) / 2.0;
	}

	cout << "iterations: " << iter << endl;
	timeutils.start("sgd");
	for (long k = 0; k < iter; ++k) {

		double lambda = 0.0;
		double gamma = 0.001 / (1.0 + k);
		double eta = (1. - alpha[k+1]) / alpha[k+2];

		for (long l = 0; l < wBatch; ++l) {
//			sgd.stepLGD(xyData, wData[l], factorDim, learnDim, lambda, gamma);
			sgd.stepNLGD(xyData, wData[l], vData[l], factorDim, learnDim, lambda, gamma, eta);
		}
	}
	timeutils.stop("sgd");

	double* w = sgd.wsum(wData, factorDim, wBatch);

	sgd.check(xyData, w, factorDim, sampleDim);

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
	Cipher* cxyData = csgd.encxyData(xyData, slots, factorDim, learnDim, wBatch);
	timeutils.stop("Enc xyData");

	timeutils.start("Enc wData");
	Cipher* cwData = csgd.encwData(wData, slots, factorDim, learnDim, wBatch);
	timeutils.stop("Enc wData");

	Cipher* cvData = new Cipher[factorDim];
	for (long i = 0; i < factorDim; ++i) {
		cvData[i] = cwData[i];
	}

	//-----------------------------------------
	for (long k = iter; k < totaliter; ++k) {

		double lambda = 0.0;
		double gamma = 0.001 / (1.0 + k);
		double eta = (1. - alpha[k+1]) / alpha[k+2];

		timeutils.start("Enc sgd step");
		csgd.encStepNLGD(cxyData, cwData, cvData, slots, factorDim, learnDim, wBatch, lambda, gamma, eta);
		timeutils.stop("Enc sgd step");
	}

	timeutils.start("Enc w out");
	Cipher* cw = csgd.encwsum(cwData, factorDim, wBatch);
	timeutils.stop("Enc w out");

	timeutils.start("Dec w");
	double* dw = csgd.decw(secretKey, cw, factorDim);
	timeutils.stop("Dec w");

	sgd.check(xyData, dw, factorDim, sampleDim);
	//-----------------------------------------
	cout << "!!! END TEST SGD !!!" << endl;
}
