# Yelp match analysis

Given a Yelp user X, this program
 * finds other Yelp users that reviewed the same business(es)
 * compares the review stars to identify users with taste similar to X's (these are "friend suggestions")
 * uses the suggested friend reviews to identify businesses that X will probably like (these are "business suggestions").

## Requirements

* the Yelp Challenge Dataset http://www.yelp.ca/dataset_challenge
* cJSON, a third-party JSON parser http://sourceforge.net/projects/cjson/

## Usage

`./ymatch <user_id> <action>`

where `<action>` is `suggest_friends` or `suggest_businesses`.


## Example use

```
./ymatch gNCf30Aow5gAW7iSBcV7GA suggest_friends > out
(head -n 1 out ; tail -n +2 out | sort -n -k 3 -t ,) > steph-friends.csv

./ymatch gNCf30Aow5gAW7iSBcV7GA suggest_businesses > out
(head -n 1 out ; tail -n +2 out | sort -n -k 3 -t ,) > steph-businesses.csv
```
