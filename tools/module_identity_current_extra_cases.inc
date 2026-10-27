// Appended ONLY to the private current-identity host fixture before run().
// MOUTH has a single editor rather than separate workflow pages; capture two
// selected response/detail views and a bypassed detail independently of defaults.
void extra_identity_states(const std::string& prefix) {
  // MOUTH has one page but several selected response/detail views.
  {Scene s(UnitKind::Mouth);
   mouth_focus_select(MouthUnit::P_SIZE);take(s,prefix+"mouth_size","size");
   mouth_focus_select(MouthUnit::P_OUT);take(s,prefix+"mouth_out","out");
   s.unit->bypass=true;take(s,prefix+"mouth_out_bypass","out-bypass");}
  // COMP title displaced the back label: tap its actual visible centre and
  // verify detail -> controls -> chain, without changing a stored parameter.
  {Scene s(UnitKind::Comp);comp_focus_select(CompUnit::P_KNEE);s.capture();
   const int dirty=mutations;const auto stored=s.unit->params[CompUnit::P_KNEE].base;
   CHECK(comp_focus_selected()==CompUnit::P_KNEE && g_overlay==Overlay::Editor);
   tap(110,14);CHECK(comp_focus_selected()==CompUnit::P_THRESHOLD && g_overlay==Overlay::Editor);
   CHECK(mutations==dirty && s.unit->params[CompUnit::P_KNEE].base==stored);
   take(s,prefix+"comp_back_controls","back-controls");
   tap(110,14);CHECK(g_overlay==Overlay::None && mutations==dirty);
   CHECK(s.unit->params[CompUnit::P_KNEE].base==stored);}
  // GRAN moved its scan/fine header actions right of the fixed title. Use
  // actual dispatch, not merely hit-box arithmetic; prove reset vs no-write
  // fine toggle with visible native text under both palette themes.
  {Scene s(UnitKind::Grain);auto& u=*static_cast<GrainUnit*>(s.unit);
   CHECK(u.clean);s.set(GrainUnit::P_SCAN,12000);
   CHECK(gran_ui::scan.x==122 && gran_ui::scan.w==52 &&
         gran_ui::fine.x==174 && gran_ui::fine.w==54);
   CHECK(gran_ui::scan.has(148,10)&&!gran_ui::fine.has(148,10));
   CHECK(gran_ui::fine.has(200,10)&&!gran_ui::scan.has(200,10));
   const int dirty=mutations;tap(148,10);
   CHECK(u.params[GrainUnit::P_SCAN].base==0 && grain_focus_selected()==GrainUnit::P_SCAN && mutations>dirty);
   take(s,prefix+"gran_scan_touch","scan-touch");CHECK(module_inspector_text("wide"));
   const int after_reset=mutations;tap(148,10);CHECK(mutations==after_reset);
   tap(200,10);CHECK(mutations==after_reset && u.params[GrainUnit::P_SCAN].base==0);
   take(s,prefix+"gran_fine_touch","fine-touch");
   CHECK(module_inspector_text("fine /32")&&!module_inspector_text("wide"));
   tap(200,10);s.capture();editor_style::capture();editor_gpu::begin();
   module_inspector_surface(false);{f::AudioHidden hidden;editor_draw();}
   CHECK(module_inspector_text("wide")&&!module_inspector_text("fine /32"));
   CHECK(mutations==after_reset);}
  std::printf("identity gesture checks: comp back twice; gran scan reset/no-op; fine toggle/revert\n");
}
