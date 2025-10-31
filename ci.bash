#!/bin/bash -xe

curl -X POST -H "Content-Type: application/json" -d @$GITHUB_EVENT_PATH $JENKINS_URL/generic-webhook-trigger/invoke
