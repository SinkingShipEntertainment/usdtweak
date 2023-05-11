name = 'usdtweak'

version = '0.1.0'

authors = [
    'richard',
]

description = '''usdtweak'''

with scope('config') as c:
    import os
    c.release_packages_path = os.environ['SSE_REZ_REPO_RELEASE_EXT']

requires = [
    "materialx",
]

private_build_requires = [
]

variants = [
    ['platform-linux', 'arch-x86_64', 'os-centos-7', "python-3.9.7", "usd-22.11.sse.2", "ptex"],
]

def pre_build_commands():
    command("source /opt/rh/devtoolset-6/enable")

def commands():
    env.REZ_USDTWEAK_ROOT = '{root}'
    env.PATH.append('{root}/bin')

uuid = 'repository.usdtweak'

