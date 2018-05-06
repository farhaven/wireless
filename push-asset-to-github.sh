#!/bin/ksh

OWNER=farhaven
REPO=wireless
TAG=${1}
NAME=wireless-${TAG}.tar.gz

if [[ ! -e "$NAME" ]]; then
	echo "$NAME does not exist." >&2
	exit 1
fi

release_id=$(curl -sS -H 'Accept: application/vnd.github.v3+json' -A "$OWNER" "https://api.github.com/repos/${OWNER}/${REPO}/releases/tags/${TAG}" | jq .id)

echo "Release ID for ${TAG}: ${release_id}"

upload_url="https://uploads.github.com/repos/${OWNER}/${REPO}/releases/${release_id}/assets?name=${NAME}"

echo "Upload URL: ${upload_url}"

curl -X POST -A ${OWNER} -u ${OWNER} "${upload_url}" -H "Content-Type: application/octet-stream" --data-binary @${NAME}
