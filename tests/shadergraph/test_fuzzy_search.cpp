// CHECK-style tests for the tab-search fuzzy matcher (FuzzySearch::match).
// Pure QString logic - no GL, no widgets, no engine.

#include "shadergraph/dialogs/fuzzysearch.h"

#include <cstdio>

static int failures = 0;

#define CHECK(cond)                                                       \
	do {                                                                  \
		if (!(cond)) {                                                    \
			std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);   \
			++failures;                                                   \
		}                                                                 \
	} while (0)

int main()
{
	using FuzzySearch::match;
	int score = 0;

	// exact substring matches, case-insensitive
	CHECK(match("norm", "Normal"));
	CHECK(match("NORM", "normal"));
	CHECK(match("mal", "Normal"));

	// substring score: earlier occurrence is better
	int atStart = 0, later = 0;
	CHECK(match("norm", "Normal", &atStart));
	CHECK(match("norm", "World Normal", &later));
	CHECK(atStart < later);

	// in-order subsequence matches ("nrm" -> "Normal")
	CHECK(match("nrm", "Normal", &score));
	CHECK(score >= 100); // scattered scores worse than any substring

	// exact substring always outranks a scattered subsequence
	int sub = 0, scattered = 0;
	CHECK(match("mul", "Multiply", &sub));
	CHECK(match("mul", "Modulate UV Left", &scattered));
	CHECK(sub < scattered);

	// order matters for subsequences
	CHECK(!match("lam", "Normal"));   // 'm' after 'a'? l..a..-, no m after a? "normaL": l is last; a then m? no
	CHECK(!match("xyz", "Normal"));

	// tighter subsequences beat gappier ones
	int tight = 0, gappy = 0;
	CHECK(match("wpos", "World Position", &gappy));
	CHECK(match("wpos", "WPosition", &tight));
	CHECK(tight < gappy);

	// empty pattern matches everything (worst score)
	CHECK(match("", "anything", &score));
	CHECK(score == 1000);

	// no match when characters are missing entirely
	CHECK(!match("texture", "vec"));

	if (failures == 0) {
		std::printf("test_fuzzy_search: all checks passed\n");
		return 0;
	}
	std::printf("test_fuzzy_search: %d check(s) FAILED\n", failures);
	return 1;
}
