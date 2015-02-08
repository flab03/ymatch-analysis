#include "assert.h"
#include "math.h"
#include "stdio.h"
#include "stdlib.h"
#include "string.h"
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "third-party/cJSON/cJSON.h"
// cJSON is an MIT licensed JSON parser. It can be obtained from
// http://sourceforge.net/projects/cjson/

/*
To compile:

export CFLAGS="-march=corei7-avx -O3 -pipe -m64 -static"
g++ $CFLAGS -std=c++0x -lm ymatch.cc third-party/cJSON/cJSON.c -o ymatch

*/

#define PREFIX \
	"../yelp_dataset_challenge_academic_dataset/yelp_academic_dataset_"

using std::map;
using std::pair;
using std::set;
using std::string;
using std::vector;

bool ReadLine(FILE *fp, string *s) {
	// TODO: Make this faster by using fgets.
	int c = fgetc(fp);
	if (c == EOF)
		return false;
	s->clear();
	s->push_back(c);
	while (true) {
		int c = fgetc(fp);
		assert(c != EOF);
		if (c == '\n')
			return true;
		s->push_back(c);
	}
}

// We want just one review per business/user pair, so ReviewKey will be the
// key of a map, and we're going to average stars if a user reviewed the same
// business twice or more.
struct ReviewKey {
	string business_id;
	string user_id;

	bool operator< (const ReviewKey &k) const {
		if (this->business_id < k.business_id)
			return true;
		if (this->business_id > k.business_id)
			return false;
		return this->user_id < k.user_id;
	}
};

struct Stars {
	double average_stars;
	long count;

	Stars() {
		average_stars = 0.0;
		count = 0;
	}
};

struct Delta {
	double average_delta;   // Average difference between stars and
	                        // the business average (currently unused).
	long count;

	Delta() {
		average_delta = 0.0;
		count = 0;
	}
};

// Loading all reviews from the dataset requires about 327 MB RAM.
// Discarding the review text helps a lot.
void LoadReviews(map<ReviewKey,Stars> *reviews) {
	reviews->clear();
	FILE *fp = popen("zcat " PREFIX "review.json.gz", "r");
	assert(fp != NULL);
	string line;
	while (ReadLine(fp, &line)) {
		cJSON *review = cJSON_Parse(line.c_str());

		cJSON *type = cJSON_GetObjectItem(review, "type");
		assert(type != NULL);
		assert(type->type == cJSON_String);
		assert(strcmp(type->valuestring, "review") == 0);

		ReviewKey key;

		cJSON *business_id = cJSON_GetObjectItem(review, "business_id");
		assert(business_id != NULL);
		assert(business_id->type == cJSON_String);
		key.business_id = business_id->valuestring;

		cJSON *user_id = cJSON_GetObjectItem(review, "user_id");
		assert(user_id != NULL);
		assert(user_id->type == cJSON_String);
		key.user_id = user_id->valuestring;

		cJSON *stars = cJSON_GetObjectItem(review, "stars");
		assert(stars != NULL);
		assert(stars->type == cJSON_Number);

		// Accumulate the stars in case a user reviewed the same business
		// multiple times.
		Stars *value = &(*reviews)[key];
		value->average_stars += stars->valuedouble;
		value->count++;

		cJSON_Delete(review);
	}
	int r = pclose(fp);
	assert(r == 0 || r == 36096);

	// Compute star averages in case a user reviewed the same business multiple
	// times.
	for (pair<const ReviewKey,Stars> &kv : *reviews)
		kv.second.average_stars /= kv.second.count;
}

void ComputeBusinessData(map<string,Stars> *businesses,
                         const map<ReviewKey,Stars> &reviews) {
	businesses->clear();
	for (const pair<const ReviewKey,Stars> &kv : reviews) {
		Stars *stars = &(*businesses)[kv.first.business_id];
		// Temporarily not the average.
		stars->average_stars += kv.second.average_stars;
		stars->count++;
	}

	for (pair<const string,Stars> &kv : *businesses) {
		// Compute the average.
		kv.second.average_stars /= kv.second.count;
	}
}

void ComputeUserData(map<string,Delta> *users,
                     const map<string,Stars> &businesses,
                     const map<ReviewKey,Stars> &reviews) {
	users->clear();
	for (const pair<const ReviewKey,Stars> &kv : reviews) {
		Delta *delta = &(*users)[kv.first.user_id];
		map<string,Stars>::const_iterator it =
			businesses.find(kv.first.business_id);
		assert(it != businesses.end());
		double business_average = it->second.average_stars;
		// Temporarily not the average.
		delta->average_delta += kv.second.average_stars - business_average;
		delta->count++;
	}

	for (pair<const string,Delta> &kv : *users) {
		// Compute the average.
		kv.second.average_delta /= kv.second.count;
	}
}

struct CommonReviewer {
	double total_error;
	long reviews_in_common;

	CommonReviewer() {
		total_error = 0.0;
		reviews_in_common = 0;
	}

	double MatchScore() const {
		// The match score is the inverse of the average absolute star
		// difference, but first we add two reviews with 1 star error each, 
		// to penalize users with few reviews in common. Then we subtract
		// 1.0 so that users with a neutral match get a score of 0.0.
		return (reviews_in_common + 2.0) / (total_error + 2.0) - 1.0;
	}
};

void FindCommonReviewers(map<string,CommonReviewer> *common_reviewers,
                         const char *user_id,
                         const map<string,Delta> &users,
                         const map<string,Stars> &businesses,
                         const map<ReviewKey,Stars> &reviews) {
	// Find businesses reviewed by user_id.
	map<string,double> reviewed_businesses;	// <business_id, review stars>
	for (const pair<const ReviewKey,Stars> &kv : reviews) {
		if (kv.first.user_id == user_id) {
			// We should only get 1 review because we already merged repeated
			// reviews in LoadReviews().
			assert(reviewed_businesses.find(kv.first.business_id) ==
			       reviewed_businesses.end());
			double *stars = &reviewed_businesses[kv.first.business_id];
			*stars = kv.second.average_stars;
		}
	}

	// Find common reviewers, e.g. users that reviewed a business reviewed by
	// user_id.
	for (const pair<const ReviewKey,Stars> &kv : reviews) {
		map<string,double>::const_iterator it =
			reviewed_businesses.find(kv.first.business_id);

		if (it != reviewed_businesses.end()) {
			// it2 contains data about the business (currently unused).
			map<string,Stars>::const_iterator it2 =
				businesses.find(kv.first.business_id);
			assert(it2 != businesses.end());

			CommonReviewer *cr = &(*common_reviewers)[kv.first.user_id];

			cr->total_error += fabs(it->second - kv.second.average_stars);
			cr->reviews_in_common++;
		}
	}
}

struct Reference {
	string reviewer_id;
	double reviewer_stars;
	double contrib;

	Reference() {
		reviewer_stars = 0;
		contrib = 0;
	}
};

struct BusinessSuggestion {
	double total_delta;
	double total_match_scores;
	long num_references;
	bool remove;		// To remove businesses that the user already reviewed.
	Reference positive_ref;
	Reference negative_ref;

	BusinessSuggestion() {
		total_delta = 0.0;
		total_match_scores = 0.0;
		num_references = 0;
		remove = false;
	}
};

void MakeBusinessSuggestions(map<string,BusinessSuggestion> *suggestions,
                             const char *user_id,
                             const map<string,CommonReviewer> common_reviewers,
                             const map<string,Stars> businesses,
                             const map<ReviewKey,Stars> &reviews) {
	suggestions->clear();
	for (const pair<const ReviewKey,Stars> &kv : reviews) {
		map<string,CommonReviewer>::const_iterator it =
			common_reviewers.find(kv.first.user_id);
		if (it == common_reviewers.end())	// Only keep common reviewers.
			continue;

		map<string,Stars>::const_iterator it2 =
			businesses.find(kv.first.business_id);
		assert(it2 != businesses.end());

		// We work with the delta between stars and the business average.
		double reviewer_delta = kv.second.average_stars -
		                        it2->second.average_stars;
		double match_score = it->second.MatchScore();
		if (match_score <= 0.0)	// Skip poorly-matching reviewers.
			continue;

		BusinessSuggestion *suggestion =
			&(*suggestions)[kv.first.business_id];
		double contrib = reviewer_delta * match_score;

		// This accumulates numbers.
		suggestion->total_delta += contrib;
		suggestion->total_match_scores += match_score;
		suggestion->num_references++;

		if (kv.first.user_id == user_id)
			suggestion->remove = true;

		// Keep track of the reviewer with the most convincing endorsement of
		// the business.
		if (contrib > suggestion->positive_ref.contrib) {
			suggestion->positive_ref.reviewer_id = kv.first.user_id;
			suggestion->positive_ref.reviewer_stars = kv.second.average_stars;
			suggestion->positive_ref.contrib = contrib;
		}
		// Keep track of the reviewer with the most convincing disapproval of
		// the business.
		if (contrib < suggestion->negative_ref.contrib) {
			suggestion->negative_ref.reviewer_id = kv.first.user_id;
			suggestion->negative_ref.reviewer_stars = kv.second.average_stars;
			suggestion->negative_ref.contrib = contrib;
		}
	}
}

void PrintFriendSuggestions(const char *user_id,
                            const map<string,CommonReviewer> &common_reviewers,
                            const map<string,Delta> &users) {
	printf("user_id,friend_id,Match score,Number of reviews,"
	       "Number of reviews in common,"
	       "Average absolute stars difference\n");
	for (pair<const string,CommonReviewer> kv : common_reviewers) {
		map<string,Delta>::const_iterator it = users.find(kv.first);
		assert(it != users.end());

		printf("%s,%s,%f,%ld,%ld,%f\n",
		       user_id, kv.first.c_str(), kv.second.MatchScore(),
		       it->second.count, kv.second.reviews_in_common,
		       kv.second.total_error / kv.second.reviews_in_common);
	}
}

void PrintBusinessSuggestions(const char *user_id,
                              const map<string,BusinessSuggestion> &suggestions,
                              const map<string,Stars> &businesses) {
	printf("user_id,business_id,Suggestion relevance,Number of references,"
	       "Total match scores,Business number of reviews,"
	       "Business average stars,"
	       "Predicted business average stars,"
	       "reviewer_id,Reviewer stars,Reviewer relevance\n");
	for (const pair<const string,BusinessSuggestion> &kv : suggestions) {
		if (kv.second.remove)
			continue;
		map<string,Stars>::const_iterator it = businesses.find(kv.first);
		assert(it != businesses.end());

		// For an underrated business, choose a positive reference.
		// For an overrated business, choose a negative reference.
		const Reference &ref = (kv.second.total_delta >= 0) ?
		                       kv.second.positive_ref :
		                       kv.second.negative_ref;

		printf("%s,%s,%f,%ld,%f,%ld,%f,%f,%s,%.1f,%f\n",
		       user_id, kv.first.c_str(), kv.second.total_delta,
		       kv.second.num_references,
		       kv.second.total_match_scores,
		       it->second.count, it->second.average_stars,
		       it->second.average_stars +
		           kv.second.total_delta / kv.second.total_match_scores,
		       ref.reviewer_id.c_str(), ref.reviewer_stars,
		       ref.contrib);
	}
}

int main(int argc, char **argv) {
	assert(argc == 3);
	const char *user_id = argv[1];
	const char *action = argv[2];

	map<ReviewKey,Stars> reviews;
	LoadReviews(&reviews);

	map<string,Stars> businesses;
	ComputeBusinessData(&businesses, reviews);

	map<string,Delta> users;
	ComputeUserData(&users, businesses, reviews);

	map<string,CommonReviewer> common_reviewers;
	FindCommonReviewers(&common_reviewers,
	                    user_id,
	                    users,
	                    businesses,
	                    reviews);

	if (strcmp(action, "suggest_friends") == 0)
		PrintFriendSuggestions(user_id, common_reviewers, users);

	map<string,BusinessSuggestion> suggestions;
	MakeBusinessSuggestions(&suggestions,
	                        user_id,
	                        common_reviewers,
	                        businesses,
	                        reviews);

	if (strcmp(action, "suggest_businesses") == 0)
		PrintBusinessSuggestions(user_id, suggestions, businesses);

	return 0;
}
