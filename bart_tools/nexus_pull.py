# Copyright 2023-2024 DreamWorks Animation LLC and Intel Corporation
# SPDX-License-Identifier: Apache-2.0


"""
Methods for pulling an artifact from a Nexus repository.

This builder is used to install jar services into the installation directory
when arras is built.
"""

import os, stat, urllib2
from ansi_colors import colorize
from SCons.Action import Action
from urllib2 import HTTPError


def NexusPull(self, target, sourceConf):
    """
    This sets up the necessary configuration for pulling jar artifacts from the
    Nexus repo in the builder.

    :Parameters:
        target : The desired output jar name.
        sourceConfig : Parameters for the source artifact.

    :Returns:
        targets : A list containing a single string path to the target.
    """

    # We'll need to be able to pass configuration parameters for the target
    # artifact to the builder, so we create a dict called NEXUS_PARAMS in the
    # build environment and add configuration data to it with targets as keys
    # and configuration dicts as the values.
    self.SetDefault(NEXUS_PARAMS={})
    self["NEXUS_PARAMS"][target] = sourceConf

    # Call the NexusBuilder with the target name and our sources (which is just
    # an empty list because we have no source files to build) then return the
    # target in a list.
    self.NexusBuilder([target], [])

    return [target]


def nexusBuilder(target, source, env):
    """
    Scons builder for pulling artifacts from a Nexus repository.

    It's expected that there be a key in the NEXUS_PARAMS environment variable
    corresponding the basename of the specified target. Since there are no
    source files to build, the source parameter is always empty.
    """

    # Define some paths we'll need.
    nexusRepo = "http://maven.anim.dreamworks.com"

    # We need to get the configuration for the target, but first, we'll need
    # to get the basename of the specified target since Scons will prepend the
    # build directory to the original target specification that was provided
    # in NexusPull(). Once we have that, we can extract the configuration dict
    # from the NEXUS_PARAMS element in the build environment.
    targetName = os.path.basename(str(target[0]))
    sourceConf = env["NEXUS_PARAMS"].get(targetName)

    # Get the version string and from this we can infer the release type for
    # the url based on whether or not the version string contains "SNAPSHOT".
    version = sourceConf.get("version")
    releaseType = "releases"
    if "SNAPSHOT" in version or version == "LATEST":
        releaseType = "snapshots"

    # Build version-specific paths the artifact we want. First, determine the
    # the content path based on the release type, then build a path to the
    # artifact based on group, artifact and version specifiers (gav).
    contentPath = "content/repositories/%s" % releaseType
    gavPath = "%s/%s/%s/%s" % ("/".join(sourceConf.get("group").split(".")),
                               sourceConf.get("service"),
                               version,
                               sourceConf.get("artifact"))

    # Build the Nexus url for the service.
    url = "%s/%s/%s" % (nexusRepo, contentPath, gavPath)


    def downloadJarFile(url):
        """
        Download the file from the Nova Nexus repo and return the actual jar
        name downloaded.
        """

        # Make the request. This will result in a redirect to the download
        # url of the service jar file.
        request = urllib2.Request(url=url)
        response = urllib2.urlopen(request)

        # Extract the jar name from the redirect url and write it.
        jar = os.path.basename(response.geturl())
        with open(jar, "wb") as jarHandle:
            jarHandle.write(response.read())

        return jar


    # Download the jar artifact and get the jar name.
    sourceJar = downloadJarFile(url)

    # We always have just one target, so pull the first element from the target
    # list and convert it to an absolute path by pre-pending the path to the
    # launch directory (typically, the top of the workspace).
    targetJar = str(target[0])

    # File mode for the target jar.
    mode = stat.S_IRUSR|stat.S_IWUSR|stat.S_IRGRP|stat.S_IWGRP|stat.S_IROTH

    # Move the jar to the target and set open permissions.
    os.rename(sourceJar, targetJar)
    os.chmod(targetJar, mode)


def generate(env):
    """SCons function for bootstrapping our code."""

    # Add our NexusPull method to the current environment.
    env.AddMethod(NexusPull)

    # Add the builder with a custom output message string.
    msg = colorize("[nexus]", "purple") + " Pulling artifact for $TARGET"
    builder = env.Builder(action=Action(nexusBuilder, msg))
    env.AppendUnique(BUILDERS={"NexusBuilder": builder})


def exists(env):
    return True


