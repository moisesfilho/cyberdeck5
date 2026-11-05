#!/usr/bin/env python3
"""Keep the offline dashboard's milestones aligned with the executable plan."""

from html.parser import HTMLParser
from pathlib import Path
import re


ROOT = Path(__file__).resolve().parents[3]
PLAN = ROOT / "docs/OS-IMPLEMENTATION-PLAN.pt-BR.md"
DASHBOARD = ROOT / "docs/OS-IMPLEMENTATION-DASHBOARD.html"
REQUIREMENTS = ROOT / "docs/OS-TAB5-REQUIREMENTS.pt-BR.md"


class Links(HTMLParser):
    def __init__(self):
        super().__init__()
        self.hrefs: list[str] = []

    def handle_starttag(self, tag: str, attrs: list[tuple[str, str | None]]) -> None:
        if tag == "a":
            self.hrefs.extend(value for name, value in attrs if name == "href" and value is not None)


def main() -> None:
    plan = PLAN.read_text(encoding="utf-8")
    html = DASHBOARD.read_text(encoding="utf-8")
    requirements = REQUIREMENTS.read_text(encoding="utf-8")
    plan_phases = re.findall(r"^### Fase (\d+) [^\n]*\n(.*?)(?=^### Fase |^## 5\.|\Z)", plan, re.M | re.S)
    dashboard_phases = re.findall(
        r'\{ name: "[^"\n]+", state: "(done|active|todo)", items: \[(.*?)\]\s*\}',
        html, re.S,
    )
    assert plan_phases and len(plan_phases) == len(dashboard_phases), "phase count differs from the plan"

    for (phase_number, phase_text), (phase_state, milestone_text) in zip(plan_phases, dashboard_phases):
        plan_states = re.findall(r"^- \[([ x])\]", phase_text, re.M)
        dashboard_states = re.findall(r'\["[^"\n]+", "(done|active|todo)"\]', milestone_text)
        assert plan_states and len(plan_states) == len(dashboard_states), f"phase {phase_number}: milestone count differs"
        for plan_state, dashboard_state in zip(plan_states, dashboard_states):
            assert (plan_state == "x") == (dashboard_state == "done"), f"phase {phase_number}: completion differs"
        expected_phase = "done" if all(state == "x" for state in plan_states) else "active" if any(
            state == "active" for state in dashboard_states
        ) else "todo"
        assert phase_state == expected_phase, f"phase {phase_number}: phase badge differs from milestones"

    plan_criteria = re.findall(r"^\| (OS-TAB5-\d{2}) \|", plan, re.M)
    dashboard_criteria = re.findall(r'\["(OS-TAB5-\d{2})", "[^"\n]+", "(?:done|active|todo)"\]', html)
    assert plan_criteria and plan_criteria == dashboard_criteria, "acceptance criteria differ from the plan"

    requirement_slices = re.findall(r"^## Recorte (\d+):", requirements, re.M)
    dashboard_slice_block = re.search(r"slices: \[(.*?)\n\s*\],\n\s*criteria:", html, re.S)
    dashboard_slices = re.findall(r'\["[^"\n]+", "(?:done|active|todo)"\]', dashboard_slice_block[1]) if dashboard_slice_block else []
    assert requirement_slices == ["1", "2", "3"] and len(dashboard_slices) == len(requirement_slices), (
        "requirements recuts differ from the dashboard"
    )

    links = Links()
    links.feed(html)
    assert links.hrefs, "dashboard has no navigation links"
    for href in links.hrefs:
        assert (DASHBOARD.parent / href).is_file(), f"broken dashboard link: {href}"

    print("PASS: OS dashboard milestones, criteria and links match the plan")


if __name__ == "__main__":
    main()
