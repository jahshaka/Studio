#include <CL/cl.h>
#include <QTimer>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QJsonArray>

#include "minerprocess.h"
#include "cuda_gpu_list.h"
#include "minerchart.h"


float RandomFloat(float a, float b) {
	float random = ((float)rand()) / (float)RAND_MAX;
	float diff = b - a;
	float r = random * diff;
	return a + r;
}

float RandomFloat()
{
	return RandomFloat(0.0f, 1.0f);
}

QString parse_vendor(const char* name) {
	if (!strcmp(name, "Intel(R) Corporation"))
		return "Intel";
	else if (!strcmp(name, "Advanced Micro Devices, Inc."))
		return "AMD";
	else if (!strcmp(name, "NVIDIA Corporation"))
		return "NVIDIA";
	else
		return "Unknown";
}

QList<GPU> get_amd_devices() {
	cl_platform_id platforms[64];
	cl_uint platforms_used;
	clGetPlatformIDs(sizeof platforms / sizeof(*platforms), platforms, &platforms_used);

	QList<GPU> ret;
	for (auto i = 0; i < platforms_used; ++i) {
		cl_device_id devices[64];
		cl_uint devices_used;
		clGetDeviceIDs(platforms[i], CL_DEVICE_TYPE_GPU, sizeof devices / sizeof(*devices), devices, &devices_used);


		for (auto j = 0u; j < devices_used; ++j) {
			char name[256];
			char vendorname[256];
			cl_ulong cache;
			cl_ulong memory;

			clGetDeviceInfo(devices[j], CL_DEVICE_GLOBAL_MEM_CACHE_SIZE, sizeof(cache), &cache, nullptr);
			clGetDeviceInfo(devices[j], CL_DEVICE_GLOBAL_MEM_SIZE, sizeof(memory), &memory, nullptr);
			clGetDeviceInfo(devices[j], CL_DEVICE_VENDOR, sizeof vendorname, &vendorname, nullptr);
			clGetDeviceInfo(devices[j], CL_DEVICE_NAME, sizeof name, &name, nullptr);
			if (parse_vendor(vendorname) == "AMD")
				ret.push_back({ i, parse_vendor(vendorname), name, GPUType::AMD });
		}
	}

	return ret;
}

QList<GPU> get_cuda_devices() {
	cl_platform_id platforms[64];
	cl_uint platforms_used;
	clGetPlatformIDs(sizeof platforms / sizeof(*platforms), platforms, &platforms_used);

	QList<GPU> ret;
	for (auto i = 0; i < platforms_used; ++i) {
		cl_device_id devices[64];
		cl_uint devices_used;
		clGetDeviceIDs(platforms[i], CL_DEVICE_TYPE_GPU, sizeof devices / sizeof(*devices), devices, &devices_used);


		for (auto j = 0u; j < devices_used; ++j) {
			char name[256];
			char vendorname[256];
			cl_ulong cache;
			cl_ulong memory;

			clGetDeviceInfo(devices[j], CL_DEVICE_GLOBAL_MEM_CACHE_SIZE, sizeof(cache), &cache, nullptr);
			clGetDeviceInfo(devices[j], CL_DEVICE_GLOBAL_MEM_SIZE, sizeof(memory), &memory, nullptr);
			clGetDeviceInfo(devices[j], CL_DEVICE_VENDOR, sizeof vendorname, &vendorname, nullptr);
			clGetDeviceInfo(devices[j], CL_DEVICE_NAME, sizeof name, &name, nullptr);
			if (parse_vendor(vendorname) == "NVIDIA")
				ret.push_back({ i, parse_vendor(vendorname), name, GPUType::AMD });
		}
	}

	return ret;
}

// code taken from autoAdjust.hpp from nvidia miner backend
/*
QList<GPU> get_cuda_devices() {
	QList<GPU> ret;

	int deviceCount = 0;
	if (cuda_get_devicecount(&deviceCount) == 0)
		return ret;

	// evaluate config parameter for if auto adjustment is needed
	for (int i = 0; i < deviceCount; i++)
	{

		nvid_ctx ctx;

		ctx.device_id = i;
		// -1 trigger auto adjustment
		ctx.device_blocks = -1;
		ctx.device_threads = -1;

		// set all evice option those marked as auto (-1) to a valid value
#ifndef _WIN32
		ctx.device_bfactor = 0;
		ctx.device_bsleep = 0;
#else
		// windows pass, try to avoid that windows kills the miner if the gpu is blocked for 2 seconds
		ctx.device_bfactor = 6;
		ctx.device_bsleep = 25;
#endif
		if (cuda_get_deviceinfo(&ctx) == 0) {
			ret.append({ ctx.device_id, "NVIDIA", ctx.device_name, GPUType::NVidia });
		}
		else
			qDebug()<<"WARNING: NVIDIA setup failed for GPU" << i;

	}

	//generateThreadConfig();
	return ret;
}
*/
bool MinerManager::initialize()
{
	// get nvidia devices
	auto list = get_cuda_devices();

	// get amd devices
	list.append(get_amd_devices());

	// create processes for each
	int portNum = 9310;
	for (auto gpu : list) {
		auto proc = new MinerProcess(this);
		proc->setGpu(gpu);
		proc->setNetworkPort(portNum++);
		processes.append(proc);
	}

	return true;

}

void MinerProcess::setNetworkPort(int portNum)
{
	networkUrl = QString("http://localhost:%1/api.json").arg(portNum);
	networkPort = portNum;
}

void MinerProcess::startMining()
{
	process = new QProcess();

	QStringList args;
	/*
	args << "--currency" << "monero7";
	args << "-o" << "pool.supportxmr.com:3333";
	args << "-p" << "x";
	args << "-r" << "x";
	args << "-u" << "43QGgipcHvNLBX3nunZLwVQpF6VbobmGcQKzXzQ5xMfJgzfRBzfXcJHX1tUHcKPm9bcjubrzKqTm69JbQSL4B3f6E3mNCbU";
	*/
	args << "--currency" << "monero7";
	args << "-o" << minerMan->poolUrl;
	args << "-p" << minerMan->password;
	args << "-r" << minerMan->identifier;
	args << "-u" << minerMan->walletId;
	args << "-i" << QString("%1").arg(this->networkPort);
	if (gpu.type == GPUType::AMD)
		args << "--noNVIDIA";
	else
		args << "--noAMD";
	args << "--noCPU";
	args << "--gpuIndex" << QString("%1").arg(gpu.index);

	data.clear();

	QObject::connect(process, &QProcess::readyReadStandardOutput, [this]()
	{
		qDebug().noquote() << QString(process->readAllStandardOutput());
	});
	QObject::connect(process, &QProcess::readyReadStandardError, [this]()
	{
		qDebug().noquote() << QString(process->readAllStandardError());
	});

	QObject::connect(process, &QProcess::errorOccurred, [this](QProcess::ProcessError error)
	{
		qDebug() << "error ocurred";

		//todo: stop mining
		this->stopMining();
	});

	process->setProcessChannelMode(QProcess::MergedChannels);
	process->start("xmr-stak.exe", args);

	// start listening over the network
	timer = new QTimer();
	timer->setInterval(1000);
	timer->start();
	QObject::connect(timer, &QTimer::timeout, [this]()
	{
		this->networkRequest(this->networkUrl, [this](QString result)
		{
			QJsonParseError error;
			auto doc = QJsonDocument::fromJson(result.toUtf8(), &error);

			if (error.error == QJsonParseError::NoError) {
				QJsonObject obj = doc.object();
				qDebug() << obj;
				//auto hps = obj["hashrate"]["total"][0].toDouble(0);
				//auto time = obj["connection"]["uptime"].toFloat(0);

				// time
				auto conObj = obj["connection"].toObject();
				auto uptime = conObj["uptime"].toInt();
				//auto t = hashArray[0].toDouble(0);

				// hps
				auto hashObj = obj["hashrate"].toObject();
				auto hashArray = hashObj["total"].toArray();
				auto hps = (float)hashArray[0].toDouble(0);

				//data.append({ uptime, hps });
				if (uptime > 0) {
					/*
					minerChart->data.append({ uptime, hps });
					if (minerChart->data.size() > 100) {
						minerChart->data.removeFirst();
					}
					*/

					emit onMinerChartData({ uptime, hps });
				}

				//minerChart->repaint();
			}
			//QJsonObject res = doc.object();
			//qDebug() << result;
		});
	});
}

void MinerProcess::stopMining()
{
	timer->stop();
	if (process != nullptr) {
		//process->terminate();
		process->kill();
		//process->close();
		//delete process;
		//process = nullptr;
	}
}

void MinerProcess::networkRequest(QString url, std::function<void(QString)> callback)
{
	qDebug() << networkUrl;
	auto reply = netMan->get(QNetworkRequest(QUrl(networkUrl)));
	
	QObject::connect(reply, &QNetworkReply::readyRead, [reply, callback]()
	{
		auto data = QString(reply->readAll());
		qDebug() << data;
		callback(data);
	});
}
