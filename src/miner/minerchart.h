/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#pragma once
#include <qwidget.h>
#include <qpainter.h>


class MinerChart : public QWidget
{
public:
	QVector<MinerChartData> data;

	MinerChart()
	{
		/*
		for (int i = 0; i < 100; i++) {
			data.append({
				true,
				i,
				RandomFloat(40, 100)
			});
		}
		*/
	}

	void paintEvent(QPaintEvent* evt)
	{

		QPainter painter(this);
		painter.setRenderHint(QPainter::Antialiasing);
		painter.setRenderHint(QPainter::HighQualityAntialiasing);

		painter.fillRect(QRect(0, 0, this->width(), this->height()), QBrush(QColor(30, 30, 30)));
	
		if (data.size() <= 1)
			return;

		float yBottom = 20;
		float yTop = this->height() - 20;
		float yDiff = yTop - yBottom;

		float xIncr = this->width() / 100.0;

		auto minHps = getMinHps();
		auto maxHps = getMaxHps();
		auto hpsDiff = maxHps - minHps;
		if (hpsDiff < 5)
			hpsDiff = 5;

		painter.setBrush(QBrush(QColor(200, 200, 255)));
		QPen pen(QColor(200, 200, 255));
		pen.setWidth(2);
		painter.setPen(pen);
		for (int i = 1; i < data.size(); i++) {
			auto d1 = data[i - 1];
			auto d2 = data[i];

			auto x1 = (i - 1) * xIncr;
			auto x2 = i * xIncr;

			auto yr = 1.0 - ((d1.hps - minHps) / hpsDiff);
			auto y1 = yBottom + yr * yDiff;

			yr = 1.0 - ((d2.hps - minHps) / hpsDiff);
			auto y2 = yBottom + yr * yDiff;
			painter.drawLine(x1, y1, x2, y2);
		}
		
	}

	float getMinHps()
	{
		float minHps = 1000000;
		for (auto& d : data)
			if (d.hps < minHps)
				minHps = d.hps;

		return minHps;
	}

	float getMaxHps()
	{
		float maxHps = 0;
		for (auto& d : data)
			if (d.hps > maxHps)
				maxHps = d.hps;

		return maxHps;
	}

	float getHpsRange()
	{
		return 0;
	}

	float RandomFloat(float a, float b)
	{
		float random = ((float)rand()) / (float)RAND_MAX;
		float diff = b - a;
		float r = random * diff;
		return a + r;
	}

	float RandomFloat()
	{
		return RandomFloat(0.0f, 1.0f);
	}
};