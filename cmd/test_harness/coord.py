# Copyright 2023-2024 DreamWorks Animation LLC and Intel Corporation
# SPDX-License-Identifier: Apache-2.0

from session import Session
from node import Node
import subprocess
import time

class Coord(object):

    def __init__(self):
        self.sessions = {}
        self.sessionList = []
        self.nodes = {}
        self.nodeList = []
        self.currentSession = None
                        
        
    def new(self,filepath=None):
        session = Session(self,len(self.sessionList))
        self.sessions[session.id] = session
        self.sessionList.append(session)
        if filepath is not None:
            session.load(filepath)
        session.show()
        return session
    
    def session(self,sessionid):
        return self.sessions[sessionid]

    def s(self,index):
        return self.sessionList[i%len(self.sessionList)]

    def addNode(self,data):
        n = Node(self,len(self.nodeList),data)
        self.nodes[n.id] = n
        self.nodeList.append(n)
        n.show()

    def node(self,nodeid):
        return self.nodes[nodeid]
    
    def n(self,i):
        return self.nodeList[i%len(self.nodeList)]

    def startNode(self,logfile):
        p = subprocess.Popen("./startNode "+logfile,shell=True)

    def waitForNodes(self,count):
        while len(self.nodeList) < count:
            time.sleep(1)
        print("{} nodes are registered".format(count))

    def startNodes(self,count):
        l = len(self.nodeList)
        for i in range(count):
            self.startNode("node{}.log".format(i))
        self.waitForNodes(l+count)
              
        
    def shutdownAll(self):
        for s in self.sessionList:
            if not s.isDeleted:
                s.delete("CEMU shutdown")
        for n in self.nodeList:
            n.shutdown("CEMU shutdown")
   
    def clientSessionReq(self):
        if self.currentSession is None:
            return None
        return self.currentSession.clientReq()
