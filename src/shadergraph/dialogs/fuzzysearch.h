/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

// Fuzzy matcher for the tab-search node palette, after NodeGraphQt's
// tab_search.py (MIT, Copyright (c) 2017 Johnny Chan), which joins the
// typed characters with ".*?" and ranks the hits. Implemented as an
// explicit subsequence scan so ranking is deterministic and testable.

#pragma once

#include <QString>

namespace FuzzySearch
{
// Returns true when every character of `pattern` occurs in `candidate`
// in order (case-insensitive). `scoreOut` is lower-is-better:
//   0..99   exact substring (earlier occurrence scores lower)
//   100+    scattered subsequence (start position + gap penalty)
inline bool match(const QString& pattern, const QString& candidate, int* scoreOut = nullptr)
{
	if (pattern.isEmpty()) {
		if (scoreOut) *scoreOut = 1000;
		return true;
	}

	const QString p = pattern.toLower();
	const QString c = candidate.toLower();

	// exact substring beats any scattered match; earlier is better
	const int idx = int(c.indexOf(p));
	if (idx >= 0) {
		if (scoreOut) *scoreOut = qMin(idx, 99);
		return true;
	}

	// in-order subsequence ("nrm" hits "Normal")
	int start = -1;
	int last = -1;
	int pi = 0;
	for (int ci = 0; ci < c.size() && pi < p.size(); ++ci) {
		if (c[ci] == p[pi]) {
			if (start < 0) start = ci;
			last = ci;
			++pi;
		}
	}
	if (pi < p.size())
		return false;

	if (scoreOut) {
		const int gaps = (last - start + 1) - int(p.size());
		*scoreOut = 100 + start + gaps * 10;
	}
	return true;
}
}
