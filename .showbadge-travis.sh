#! /bin/sh
# ShowBadge script for Travis-CI
# Version: ShowBadge v0.1.0
# Author: Hang Park <hangpark@kaist.ac.kr>

# If the build is of a pull request
if [ $TRAVIS_PULL_REQUEST != "false" ]; then
  echo "[ShowBadge] Pull request builds are not supported."
  exit 1
fi

# If server is not specified
if [ -z $SHOWBADGE_SERVER ]; then
  echo "[ShowBadge] Server is not specified."
  exit 1
fi

# If key-value pair is not given
if [ -z "$1" ] || [ -z "$2" ]; then
  echo "[ShowBadge] Key-value pair is not given."
  exit 1
fi

# Print data to send
echo "[ShowBadge] Push following data into the server:"
echo ""
echo "  Server       $SHOWBADGE_SERVER"
echo "  User/Repo    $TRAVIS_REPO_SLUG"
echo "  Commit       $TRAVIS_COMMIT"
echo "  Key          $1"
echo "  Value        $2"
echo ""

# Send data
curl -X POST -d "commit=$TRAVIS_COMMIT&key=$1&value=$2" \
  ${SHOWBADGE_SERVER%%/}/$TRAVIS_REPO_SLUG/

exit 0
