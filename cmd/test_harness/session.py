# Copyright 2023-2024 DreamWorks Animation LLC and Intel Corporation
# SPDX-License-Identifier: Apache-2.0

import uuid
import json
import time
            
class Computation(object):

    def __init__(self,session,basename,config,prev,arrayIndex=None):
        self.session = session
        self.basename = basename
        self.arrayIndex = arrayIndex
        if arrayIndex is None:
            self.name = basename
        else:
            self.name = "{}_id_{}".format(basename,arrayIndex)
        prev = prev.get(self.name)
        self.config = config
        self.id = prev.id if prev else None
        self.nodeid = prev.nodeid if prev else None
        self.ready = prev.ready if prev else False
        self.isClient = (basename == '(client)')
        self.exitKillsSession = prev.exitKillsSession if prev else False

    def evalConfig(self):
        newconfig = self.config.copy()
        for (k,v) in self.config.iteritems():
            if not isinstance(v,basestring):
                continue
            if v == "$arrayIndex":
                newconfig[k] = self.arrayIndex
            elif v == "$arrayNumber":
                newconfig[k] = self.session.arrayLen(self.basename)
            elif v.startswith("$arrayNumber."):
                arrayName = v[len("$arrayNumber."):]
                newconfig[k] = self.session.arrayLen(arrayName)
        self.config = newconfig
        
    def show(self,prefix=""):
        print("{}{} id {} on {}".format(prefix,self.name,self.id,self.nodeid))

    def alloc(self,node,force=False):
        if force or self.id is None:
            self.id = str(uuid.uuid4())
        if force or self.nodeid is None:
            self.nodeid = node.id

    # make config for routing section
    def makeRConfig(self):
        return { 'compId':self.id,
                 'hostId':self.id,
                 'nodeId':self.nodeid }
        
class Session(object):

    def __init__(self,coord,index):
        self.coord = coord
        self.index = index
        self.id = str(uuid.uuid4())
        self.computations = {}
        self.compsById = {}
        self.arrays = {}
        self.messageFilter = {}
        self.isDeleted = False
        self.entryId = None
        self.filepath = None
        self.autoDelete = True
        
    def show(self,prefix=""):
        print("{}({}) session id {}".format(prefix,self.index,self.id))
        if self.filepath is not None:
            print("Loaded from {}",filepath)
        for c in self.computations.values():
            c.show(prefix+"  ")
        if self.entryId:
            entryNode = self.coord.node(self.entryId)
            print("entry: {}:{}".format(entryNode.hostname,entryNode.port))

    def makeCurrent(self):
        self.coord.currentSession = self

    def arrayLen(self,basename):
        return len(self.arrays[basename])
    
    def load(self,filepath):
        f = open(filepath)
        self.definition = json.load(f)
        self.rebuild()

    def rebuild(self):
        prev = self.computations
        self.computations = {}
        for (name,config) in self.definition['computations'].iteritems():
            if config.get('skip'):
                pass
            elif 'arrayExpand' in config:
                self.addArray(name,config,config['arrayExpand'],prev)
            else:
                self.addComp(name,config,prev)

        # evaluate configs and
        # build 'reversed' source,target message filter
        # using the target,source data in the comp configs
        self.messageFilter = {}
        for c in self.computations.values():
            c.evalConfig()
            target = c.name
            if 'messages' in c.config:
                for (source,names) in c.config['messages'].iteritems():
                    # t,s data in config supports '*' for names,
                    # s,t data requires an empty object {} for the same effect
                    if names == '*': names = {}
                    if source in self.arrays:
                        for s in self.arrays[source]:
                            entry = self.messageFilter.setdefault(s,{})
                            entry[target] = names
                    else:
                        entry = self.messageFilter.setdefault(source,{})
                        entry[target] = names
            

    def addArray(self,basename,config,count,prev):
        names = []
        for i in range(count):
            c = self.addComp(basename,config,prev,i)
            names.append(c.name)
        self.arrays[basename] = names
        
    def addComp(self,basename,config,prev,arrayIndex=None):
        c = Computation(self,basename,config,prev,arrayIndex)
        self.computations[c.name] = c
        return c

    def alloc(self):
        i = 0
        self.compsById = {}
        for c in self.computations.values():
            if c.isClient:
                continue
            node = self.coord.n(i)
            c.alloc(node)
            self.compsById[c.id] = c
            entry = c.config.get('entry')
            if entry == True or entry == "yes":
                self.entryId = c.nodeid
            i = i+1
        self.show()
            
    def getNodes(self):
        nodes = set()
        for c in self.computations.values():
            if c.nodeid:
                nodes.add(self.coord.node(c.nodeid))
        return nodes
        
    def launch(self):
        (ncs,r) = self.genNodeReqData()
        for (nid,nc) in ncs.iteritems():
            nd = { nid: nc,
                   "routing": r }
            node = self.coord.node(nid)
            node.launch(self.id,nd)
        if self.coord.currentSession is None:
            self.makeCurrent()
        self.readyCount = 0
        for c in self.computations.values():
            if c.ready:
                self.readyCount += 1;
        print("Initial ready count: {}".format(self.readyCount))

    def modify(self):
        (ncs,r) = self.genNodeReqData()
        for (nid,nc) in ncs.iteritems():
            nd = { nid: nc,
                   "routing": r }
            node = self.coord.node(nid)
            node.modify(self.id,nd)

    def waitForReady(self):
        while self.readyCount < len(self.computations)-1:
            time.sleep(1)
        print("Session {} ready".format(self.id))

    def go(self):
        (ncs,r) = self.genNodeReqData()
        for (nid,nc) in ncs.iteritems():
            nd = { "status":"run",
                   "routing": r }
            node = self.coord.node(nid)
            node.setStatus(self.id,nd)

    def engineReady(self):
        for node in self.getNodes():
            nd = { "status":"engineReady"}
            node.setStatus(self.id,nd)
            
    def status(self):
        print("Session {}".format(self.id))
        for node in self.getNodes():
            print("  on node {}".format(node.id))
            s = node.getStatus(self.id)
            print("     Session: {}".format(s["state"]))
            for (cn,cs) in s["computations"].iteritems():
                print ("     {} : {}".format(cn,cs))
                  

    def delete(self,reason="CEMU request"):
        for node in self.getNodes():
            node.deleteSession(self.id,reason)
        self.isDeleted = True
        
    def hostReady(self,hostId):
        c = self.compsById[hostId]
        c.ready = True
        print "{} is ready".format(c.name)
        self.readyCount += 1
        
    def genNodeReqData(self):
        nodeConfigs = {}
        nodes = {}
        comps = {}
        for c in self.computations.values():
            if c.isClient:
                continue
            if c.id == None or c.nodeid == None:
                print("Warning: computation {} has no assignment, omitting".format(c.name))
                continue
            comps[c.name] = c.makeRConfig()
            if c.nodeid in nodeConfigs:
                nodeConfig = nodeConfigs[c.nodeid]
            else:
                node = self.coord.node(c.nodeid)
                nodeConfig = node.makeConfig()
                nodeConfig['config']['sessionId'] = self.id
                nodeConfigs[c.nodeid] = nodeConfig
                nodes[node.id] = node.makeRConfig()

            nodeConfig['config']['computations'][c.name] = c.config
            entry = c.config.get('entry')
            if entry == True or entry == "yes":
                entryNode = c.nodeid
           
                
        nodes[entryNode]['entry'] = True
        routing = { 'messageFilter': self.messageFilter,
                    self.id: {
                        "nodes": nodes,
                        "computations": comps,
                        "engine":"empty",
                        "clientData": {
                            "session":"empty",
                            "clientInfo":{
                                "nodeName":None,
                                "osVersion":None,
                                "platformModel":None,
                                "platformName":None,
                                "osName":None
                                }
                            }
                        }
                    }
        return (nodeConfigs,routing)

        
        
    def clientReq(self):
        entryNode = self.coord.node(self.entryId)
        return {'sessionId':self.id,
                'hostname': entryNode.hostname,
                'ip':entryNode.hostip,
                'port': entryNode.port}
                
                
    def deleteRequest(self,reason):
        print("Requested to delete session {}, reason {}".format(self.id,reason))
        if self.autoDelete:
            print("Deleting")
            self.delete(reason)
        else:
            print("Ignoring request")

    def hostExit(self,compId,reason):
        if compId not in self.compsById:
            # this was termination of a defunct computation
            print("Defunct computation {} terminated, reason {}".format(compId,reason))
            return;
        c = self.compsById[compId]
        print("Computation exit {}, reason {}".format(c.name,reason))
        if c.exitKillsSession:
            print("Killing session in response")
            self.delete(reason)
        else:
            print("Continuing")
              
