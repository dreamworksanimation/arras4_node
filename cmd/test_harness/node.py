# Copyright 2023-2024 DreamWorks Animation LLC and Intel Corporation
# SPDX-License-Identifier: Apache-2.0

import requests
import socket
import json
import pprint
import time
    
class Node(object):

    def __init__(self,coord,index,data):
        self.coord = coord
        self.index = index
        self.id = data["id"]
        self.hostname = data["hostname"]
        self.hostip = data["ipAddress"]
        self.httpPort = data["httpPort"]
        self.port = data["port"]


    def show(self,prefix=""):
        print("{}({}) node id {} on {}".format(prefix,self.index,self.id,self.hostname))

    # make config for main section
    # add 'computations' and 'sessionId' to 'config'
    def makeConfig(self):
        return { 'tcp':self.port,
                 'port':self.httpPort,
                 'ip':self.hostip,
                 'nodeId':self.id,
                 'config': { 'computations': {}}
                 }

    # make config for routing section
    # add 'entry: true/false' 
    def makeRConfig(self):
        return { 'tcp':self.port,
                 'port':self.httpPort,
                 'host':self.hostname
                 }

    # sends a POST /sessions request to the node
    def launch(self,sessionId,data):
        url = "http://{}:{}/sessions".format(self.hostname,self.httpPort)
        r = requests.post(url,json=data)
        r.raise_for_status()
        print "Launch {} on node {}: {}".format(sessionId,self.id,r.text)

    # sends a PUT /sessions/modify request to the node
    def modify(self,sessionId,data):
        url = "http://{}:{}/sessions/modify".format(self.hostname,self.httpPort)
        r = requests.put(url,json=data)
        r.raise_for_status()
        print "Modify {} on node {}: {}".format(sessionId,self.id,r.text)

    def setStatus(self,sessionId,data):
        url = "http://{}:{}/sessions/{}/status".format(self.hostname,self.httpPort,sessionId)
        while True:
            r = requests.put(url,json=data)
            if r.status_code != 409:  # HTTP_CONFLICT
                break
            print("Session {} is busy, retry in 1 second".format(sessionId))
            time.sleep(1)
        r.raise_for_status()
        print "Run {} on node {}: {}".format(sessionId,self.id,r.text)
        
    def getStatus(self,sessionId):
        url = "http://{}:{}/node/1/sessions/{}/status".format(self.hostname,self.httpPort,sessionId)
        r = requests.get(url)
        r.raise_for_status()
        return r.json()

    def deleteSession(self,sessionId,reason):
        url = "http://{}:{}/sessions/{}".format(self.hostname,self.httpPort,sessionId)
        r = requests.delete(url,headers={"X-Session-Delete-Reason":reason})
        #r.raise_for_status()
        print "Delete {} on node {}: {}".format(sessionId,self.id,r.text)
        
    def shutdown(self,reason="CEMU request"):
        url = "http://{}:{}/status".format(self.hostname,self.httpPort)
        body = {'status':'shutdown',
                'shutdownByApp':'cemu',
                'shutdownByUser':'NA',
                'shutdownReason':reason}
        r = requests.put(url,json=body)
        #r.raise_for_status()
        print "Node {} shutdown: {}".format(self.id,r.text)
    
