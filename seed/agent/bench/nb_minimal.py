"""TEST-ONLY launcher: nanobot's own CLI, reduced to omagent's four tools for the equal-work benchmark.

nanobot 0.3.5 has no config switch for every extra tool (message, spawn, cron, session and goal
tools stay registered; see the feature matrix), so this wrapper drops every tool except
read_file, write_file, exec and web_fetch at registration. Everything else is unmodified nanobot.
If NB_MODULES is set, the files of all loaded modules are written there at exit (footprint run only).
"""
import atexit, os, sys

from nanobot.agent.tools.registry import ToolRegistry

KEEP = {"read_file", "write_file", "exec", "web_fetch"}
_register = ToolRegistry.register


def register(self, tool):
    if tool.name in KEEP:
        _register(self, tool)


ToolRegistry.register = register

if os.environ.get("NB_MODULES"):
    def dump():
        with open(os.environ["NB_MODULES"], "w") as fh:
            for m in list(sys.modules.values()):
                f = getattr(m, "__cached__", None) or getattr(m, "__file__", None)
                if f and not os.path.exists(f):
                    f = getattr(m, "__file__", None)
                if f:
                    fh.write(f + "\n")
    atexit.register(dump)

from nanobot.cli.entry import main  # noqa: E402

sys.argv[0] = "nanobot"
sys.exit(main())
