#!/usr/bin/env python3
# Authors: Ambuj Varshney <ambuj@tiku-os.org>
# SPDX-License-Identifier: Apache-2.0
"""Select document providers and assign format adapters to optional archives."""
from pathlib import Path

BASE_KITS = ('editor', 'application', 'translation', 'interface', 'storage', 'device', 'media', 'support')
OPTIONAL_KITS = ('deck_formats', 'document_formats', 'office_shapes', 'deck', 'document')
LINK_ORDER = ('editor', 'application', 'deck_formats', 'document_formats', 'office_shapes',
              'translation', 'deck', 'document', 'interface', 'storage', 'device', 'media', 'support')
REQUIRES = {
    'document': {'interface', 'support'},
    'document_formats': {'document', 'translation', 'interface', 'storage', 'support'},
    'deck': {'document', 'interface', 'support'},
    'deck_formats': {'office_shapes', 'deck', 'document_formats', 'translation', 'interface', 'support'},
    'office_shapes': {'deck', 'translation', 'interface', 'support'},
}


def file_kit(name):
    """Return the archive owning a logical kit source or header path."""
    path = Path(name)
    if path.parts[1] != 'translation':
        return path.parts[1]
    if path.name.startswith('tiku_office_shapes'):
        return 'office_shapes'
    if path.name.startswith(('tiku_rich', 'tiku_docx', 'tiku_odt', 'tiku_office_properties')):
        return 'document_formats'
    if path.name.startswith('tiku_deck'):
        return 'deck_formats'
    return 'translation'


def validate_profile(profile):
    """Require one supported document provider and compatible presentation selection."""
    provider = profile.get('document_provider', 'none')
    if not isinstance(provider, str) or provider not in {'none', 'tiku-basic', 'kintsugi-advanced'}:
        raise RuntimeError('Select exactly one supported document provider')
    presentation = profile.get('presentation_provider', 'none')
    if not isinstance(presentation, str) or presentation not in {'none', 'kintsugi-deck'}:
        raise RuntimeError('Unsupported presentation provider')
    if presentation != 'none' and provider != 'kintsugi-advanced':
        raise RuntimeError('Presentation provider requires the advanced document provider')
    if profile.get('office_shapes') and presentation == 'none':
        raise RuntimeError('Office shapes require the presentation provider')
    return provider, presentation


def validate_selection(provider, kits):
    """Reject archives that do not match the selected document layout."""
    if provider not in {'none', 'tiku-basic', 'kintsugi-advanced'}:
        raise RuntimeError('Unsupported document provider')
    optional = set(kits) & set(OPTIONAL_KITS)
    if provider == 'none' and optional:
        raise RuntimeError('Base SDK cannot export document or presentation archives')
    if provider != 'none' and not {'document', 'document_formats'} <= optional:
        raise RuntimeError('Document provider requires paired model and format archives')
    if provider == 'tiku-basic' and optional - {'document', 'document_formats'}:
        raise RuntimeError('Basic document provider cannot export presentation archives')
    for kit in optional:
        missing = REQUIRES[kit] - set(kits)
        if missing:
            raise RuntimeError('Missing dependencies for ' + kit + ': ' + ', '.join(sorted(missing)))
