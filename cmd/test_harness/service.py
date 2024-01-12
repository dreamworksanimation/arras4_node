# Copyright 2023-2024 DreamWorks Animation LLC and Intel Corporation
# SPDX-License-Identifier: Apache-2.0

from tornado.ioloop import IOLoop, PeriodicCallback
import tornado.web

import json

PORT = 8888
class CEMUService(tornado.web.Application):

     def __init__(self, coord):
        
          handlers = [
             (r"/status",StatusHandler,dict(coord=coord)),
             (r"/coordinator/1/nodes",NodesHandler,dict(coord=coord)),
             (r"/coordinator/1/nodes/([a-fA-F\-0-9]+)",NodesHandler,dict(coord=coord)),
             (r"/coordinator/1/sessions",SessionsHandler,dict(coord=coord)),
             (r"/coordinator/1/sessions/([a-fA-F\-0-9]+)",SessionsHandler,dict(coord=coord)),
             (r"/coordinator/1/sessions/([a-fA-F\-0-9]+)/hosts/([a-fA-F\-0-9]+)",HostsHandler,dict(coord=coord)),
             (r"/coordinator/1/sessions/([a-fA-F\-0-9]+)/computations/([a-fA-F\-0-9]+)",HostsHandler,dict(coord=coord))
          ]

          super(CEMUService, self).__init__(
               handlers = handlers)

     def run(self):
          self.listen(PORT)
          self.ioLoop = IOLoop.current()
          self.ioLoop.start()

     def stop(self):
          self.ioLoop.stop()

class BaseHandler(tornado.web.RequestHandler):

    def initialize(self,coord):
         self.coord = coord


class StatusHandler(BaseHandler):

    def get(self):
        response = { 'sessions': len(self.coord.sessions) }
        self.write(response)

        
class NodesHandler(BaseHandler):

    def post(self):
        req = self.request.body
        # remove excess
        req = req[:req.rfind('}')+1]
        req = json.loads(req)
        self.coord.addNode(req)

    def delete(self,nodeId):
        print("Request to delete node {}".format(nodeId))
        self.set_status(204) # NO CONTENT
              
class SessionsHandler(BaseHandler):

    def delete(self,sessId):
         reason = "Unknown"
         if 'X-Session-Delete-Reason' in self.request.headers:
             reason = self.request.headers['X-Session-Delete-Reason']
         self.coord.session(sessId).deleteRequest(reason)
         self.set_status(204) # NO CONTENT

    def post(self):
         # client session request
         resp = self.coord.clientSessionReq()
         if resp:
              print("client connecting : sending {}".format(resp))
              self.write(resp)
         else:
              self.set_status(503) # service unavailable
              
class HostsHandler(BaseHandler):

    def put(self,sessId,hostId):
        # node is notifying us that a computation(host)
        # is ready
        self.coord.session(sessId).hostReady(hostId)

    def delete(self,sessId,hostId):
        # node is notifying us that a computation(host)
        # exited
        reason = "Unknown"
        if 'X-Host-Delete-Reason' in self.request.headers:
             reason = self.request.headers['X-Host-Delete-Reason']
        self.coord.session(sessId).hostExit(hostId,reason)
