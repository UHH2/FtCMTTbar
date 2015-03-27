#include <iostream>
#include <memory>

#include "UHH2/core/include/AnalysisModule.h"
#include "UHH2/core/include/Event.h"
#include "UHH2/common/include/EventHists.h"
#include "UHH2/common/include/CleaningModules.h"
#include "UHH2/common/include/NSelections.h"
#include "UHH2/common/include/MuonHists.h"
#include "UHH2/common/include/MuonIds.h"
#include "UHH2/common/include/ElectronHists.h"
#include "UHH2/common/include/ElectronIds.h"
#include "UHH2/common/include/TauHists.h"
#include "UHH2/common/include/TauIds.h"
#include "UHH2/common/include/JetHists.h"
#include "UHH2/common/include/JetIds.h"
#include "UHH2/FtCMTTbar/include/FtCMTTbarSelections.h"
#include "UHH2/FtCMTTbar/include/FtCMTTbarUtils.h"
#include "UHH2/FtCMTTbar/include/FtCMTTbarPreSelectionHists.h"
#include "UHH2/common/include/JetCorrections.h"
#include "UHH2/common/include/ObjectIdUtils.h"
#include "UHH2/common/include/Utils.h"
#include "UHH2/core/include/Selection.h"

using namespace uhh2;

class FtCMTTbarPreSelectionModule: public AnalysisModule {

 public:
  explicit FtCMTTbarPreSelectionModule(Context & ctx);
  virtual bool process(Event & event) override;

 private:
  std::string channel_;

  std::unique_ptr<MuonCleaner> muo_cleaner;
  std::unique_ptr<ElectronCleaner> ele_cleaner;

  std::unique_ptr<JetCorrector> jet_corrector;
  std::unique_ptr<JetLeptonCleaner> jetlepton_cleaner;
  std::unique_ptr<JetCleaner> jet_cleaner;

  std::unique_ptr<TopJetCorrector> topjet_corrector;
  std::unique_ptr<TopJetLeptonDeltaRCleaner> topjetlepton_cleaner;
  std::unique_ptr<TopJetCleaner> topjet_cleaner;

  // declare the Selections to use
  std::unique_ptr<Selection> muo1_sel, ele1_sel, jet1_sel, jet2_sel, topjet1_sel;

  // store the Hists collection as member variables
  std::unique_ptr<Hists> input_h_event, input_h_jet, input_h_ele, input_h_muo, input_h_topjet;
  std::unique_ptr<Hists> output_h_event, output_h_jet, output_h_ele, output_h_muo, output_h_topjet;
};

FtCMTTbarPreSelectionModule::FtCMTTbarPreSelectionModule(Context & ctx){

  channel_ = ctx.get("channel", "lepton");
  if(channel_!="muon" && channel_!="electron" && channel_!="lepton")
    throw std::runtime_error("undefined argument for 'channel' key in xml file (must be 'muon', 'electron' or 'lepton'): "+channel_);

  // setup object cleaners
  muo_cleaner.reset(new MuonCleaner(AndId<Muon>(MuonIDTight(), PtEtaCut(45., 2.1))));
  ele_cleaner.reset(new ElectronCleaner(AndId<Electron>(ElectronID_PHYS14_25ns_tight_noIso, PtEtaCut(50., 2.5))));

  jet_corrector.reset(new JetCorrector(JERFiles::PHYS14_L123_MC));
  jetlepton_cleaner.reset(new JetLeptonCleaner(JERFiles::PHYS14_L123_MC));
  jetlepton_cleaner->set_drmax(.4);
  jet_cleaner.reset(new JetCleaner(30., 2.4));

  topjet_corrector.reset(new TopJetCorrector(JERFiles::PHYS14_L123_MC));
  topjetlepton_cleaner.reset(new TopJetLeptonDeltaRCleaner(.8));
  topjet_cleaner.reset(new TopJetCleaner(TopJetId(PtEtaCut(200., 2.4))));

  // set up selections
  muo1_sel.reset(new NMuonSelection(1));      // at least 1 muon
  ele1_sel.reset(new NElectronSelection(1));  // at least 1 electron
  jet1_sel.reset(new NJetSelection(1));       // at least 1 jet
  jet2_sel.reset(new NJetSelection(2));       // at least 2 jets
  topjet1_sel.reset(new NTopJetSelection(1)); // at least 1 topjet

  // set up Hists classes
  input_h_event.reset(new EventHists(ctx, "input_Event"));
  input_h_jet.reset(new JetHists(ctx, "input_Jets"));
  input_h_ele.reset(new ElectronHists(ctx, "input_Electrons"));
  input_h_muo.reset(new MuonHists(ctx, "input_Muons"));
  input_h_topjet.reset(new TopJetHists(ctx, "input_TopJets"));

  output_h_event.reset(new EventHists(ctx, "output_Event"));
  output_h_jet.reset(new JetHists(ctx, "output_Jets"));
  output_h_ele.reset(new ElectronHists(ctx, "output_Electrons"));
  output_h_muo.reset(new MuonHists(ctx, "output_Muons"));
  output_h_topjet.reset(new TopJetHists(ctx, "output_TopJets"));
}

bool FtCMTTbarPreSelectionModule::process(Event & event) {

  // dump input content
  input_h_event->fill(event);
  input_h_jet->fill(event);
  input_h_ele->fill(event);
  input_h_muo->fill(event);
  input_h_topjet->fill(event);

  // LEPTON CLEANING
  muo_cleaner->process(event);
  ele_cleaner->process(event);

  // LEPTON PRE-SELECTION
  bool pass_lep(false);
  if(channel_ == "lepton") pass_lep = (muo1_sel->passes(event) || ele1_sel->passes(event));
  else if(channel_ == "muon") pass_lep = muo1_sel->passes(event);
  else if(channel_ == "electron") pass_lep = ele1_sel->passes(event);
  else throw std::runtime_error("undefined argument for 'channel' key in xml file (must be 'muon', 'electron' or 'lepton'): "+channel_);

  // exit if lepton selection fails, otherwise proceed to jet selection
  if(!pass_lep) return false;

  // keep Jets *before cleaning* to store them in the ntuple if event is accepted
  std::unique_ptr< std::vector<Jet> > uncleaned_jets(new std::vector<Jet>(*event.jets));
  std::unique_ptr< std::vector<TopJet> > uncleaned_topjets(new std::vector<TopJet>(*event.topjets));

  // JET CLEANING
  jet_corrector->process(event);
  jetlepton_cleaner->process(event);
  jet_cleaner->process(event);
  topjet_corrector->process(event);
  topjetlepton_cleaner->process(event);
  topjet_cleaner->process(event);

  // JET PRE-SELECTION
  bool pass_jet = jet2_sel->passes(event) || (jet1_sel->passes(event) && topjet1_sel->passes(event));

  // exit if jet preselection fails
  if(!pass_jet) return false;

  // store Jets *before cleaning* in the ntuple
  event.jets->clear();
  event.jets->reserve(uncleaned_jets->size());
  for(auto & j : *uncleaned_jets) event.jets->push_back(j); 
  sort_by_pt<Jet>(*event.jets);

  event.topjets->clear();
  event.topjets->reserve(uncleaned_topjets->size());
  for(auto & j : *uncleaned_topjets) event.topjets->push_back(j); 
  sort_by_pt<TopJet>(*event.topjets);

  // dump output content
  output_h_event->fill(event);
  output_h_jet->fill(event);
  output_h_ele->fill(event);
  output_h_muo->fill(event);
  output_h_topjet->fill(event);

  return true;
}

UHH2_REGISTER_ANALYSIS_MODULE(FtCMTTbarPreSelectionModule)
